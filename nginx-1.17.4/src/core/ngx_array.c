
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


ngx_array_t *
ngx_array_create(ngx_pool_t *p, ngx_uint_t n, size_t size)
{
    ngx_array_t *a;
    /* 在内存池 pool上面 分配一段内存给 ngx_array数据结构*/
    a = ngx_palloc(p, sizeof(ngx_array_t));
    if (a == NULL) {
        return NULL;
    }
    /**
     *  数组初始化，并且分配内存空间给数组元素
     *  PS:这边数组的数据结构和数组元素的存储分成了两次在pool上分配，笔者认为可以一次进行分配
     *  但是Nginx是多进程的，程序执行流程是线性的，所以分两次分配也无伤大雅。
     */
    if (ngx_array_init(a, p, n, size) != NGX_OK) {
        return NULL;
    }

    return a;
}

/*
数组销毁设计的也挺讲究的，会去检查数组是否在内存池内存块上的结尾部分，如果在结尾部分，则将内存回收给内存池。
如果不在结尾 那么不做动作
*/
void
ngx_array_destroy(ngx_array_t *a)
{
    ngx_pool_t  *p;

    p = a->pool;

    if ((u_char *) a->elts + a->size * a->nalloc == p->d.last) {
        p->d.last -= a->size * a->nalloc;
    }

    if ((u_char *) a + sizeof(ngx_array_t) == p->d.last) {
        p->d.last = (u_char *) a;
    }
}

/**
 * 添加一个元素
 * 返回一个指针 这个指针可以存放大小为size的元素
 */
void *
ngx_array_push(ngx_array_t *a)
{
    void        *elt, *new;
    size_t       size;
    ngx_pool_t  *p;
    /* 如果数组中元素都用完了 ，则需要对数组进行扩容 */
    if (a->nelts == a->nalloc) {

        /* the array is full */

        size = a->size * a->nalloc;

        p = a->pool;
        /**
         * 扩容有两种方式
         * 1.如果数组元素的末尾和内存池pool的可用开始的地址相同，
         * 并且内存池剩余的空间支持数组扩容，则在当前内存池上扩容
         * 2. 如果扩容的大小超出了当前内存池剩余的容量或者数组元素的末尾和内存池pool的可用开始的地址不相同，
         * 则需要重新分配一个新的内存块存储数组，并且将原数组拷贝到新的地址上
         */
        if ((u_char *) a->elts + size == p->d.last
            && p->d.last + a->size <= p->d.end)
        {
            /*
             * the array allocation is the last in the pool
             * and there is space for new allocation
             */

            p->d.last += a->size;
            a->nalloc++;

        } else {
            /* allocate a new array */

            new = ngx_palloc(p, 2 * size);
            if (new == NULL) {
                return NULL;
            }

            ngx_memcpy(new, a->elts, size);
            a->elts = new;
            a->nalloc *= 2;
        }
    }
    //还有空间容纳元素
    elt = (u_char *) a->elts + a->size * a->nelts;
    a->nelts++;

    return elt;
}


/**
 * 这个方法同上，只不过支持多个元素
 */
void *
ngx_array_push_n(ngx_array_t *a, ngx_uint_t n)
{
    void        *elt, *new;
    size_t       size;
    ngx_uint_t   nalloc;
    ngx_pool_t  *p;

    size = n * a->size;

    if (a->nelts + n > a->nalloc) {

        /* the array is full */

        p = a->pool;

        if ((u_char *) a->elts + a->size * a->nalloc == p->d.last
            && p->d.last + size <= p->d.end)
        {
            /*
             * the array allocation is the last in the pool
             * and there is space for new allocation
             */

            p->d.last += size;
            a->nalloc += n;

        } else {
            /* allocate a new array */

            nalloc = 2 * ((n >= a->nalloc) ? n : a->nalloc);

            new = ngx_palloc(p, nalloc * a->size);
            if (new == NULL) {
                return NULL;
            }

            ngx_memcpy(new, a->elts, a->nelts * a->size);
            a->elts = new;
            a->nalloc = nalloc;
        }
    }

    elt = (u_char *) a->elts + a->size * a->nelts;
    a->nelts += n;

    return elt;
}
