
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>

/**
 * 创建一个缓冲区。需要传入pool和buf的大小
 */
ngx_buf_t *
ngx_create_temp_buf(ngx_pool_t *pool, size_t size)
{
    ngx_buf_t *b;

    /*
       ngx_calloc_buf 展开是 ngx_pcalloc(pool, sizeof(ngx_buf_t))
    */
    b = ngx_calloc_buf(pool);
    if (b == NULL) {
        return NULL;
    }
    /* 分配缓冲区内存;  pool为内存池，size为buf的大小*/
    b->start = ngx_palloc(pool, size);
    if (b->start == NULL) {
        return NULL;
    }

    /*
     * set by ngx_calloc_buf():
     *
     *     b->file_pos = 0;
     *     b->file_last = 0;
     *     b->file = NULL;
     *     b->shadow = NULL;
     *     b->tag = 0;
     *     and flags
     */

    b->pos = b->start; //待处理数据的标记指针
    b->last = b->start;//待处理数据的结尾标记指针
    b->end = b->last + size;//缓冲区结尾地址
    b->temporary = 1;

    return b;
}

/*创建一个缓冲区的链表结构 ngx_alloc_chain_link*/
ngx_chain_t *
ngx_alloc_chain_link(ngx_pool_t *pool)
{
    ngx_chain_t  *cl;

    cl = pool->chain;
    /*
	 * 首先从内存池中去取ngx_chain_t，
	 * 被清空的ngx_chain_t结构都会放在pool->chain 缓冲链上
	 */
    if (cl) {
        pool->chain = cl->next;
        return cl;
    }
    /* 如果取不到，则从内存池pool上分配一个数据结构  */
    cl = ngx_palloc(pool, sizeof(ngx_chain_t));
    if (cl == NULL) {
        return NULL;
    }

    return cl;
}


ngx_chain_t *
ngx_create_chain_of_bufs(ngx_pool_t *pool, ngx_bufs_t *bufs)
{
    u_char       *p;
    ngx_int_t     i;
    ngx_buf_t    *b;
    ngx_chain_t  *chain, *cl, **ll;
    /* 在内存池pool上分配bufs->num个 buf缓冲区 ，每个大小为bufs->size */
    p = ngx_palloc(pool, bufs->num * bufs->size);
    if (p == NULL) {
        return NULL;
    }

    ll = &chain;
    /* 循环创建BUF，并且将ngx_buf_t挂载到ngx_chain_t链表上，并且返回链表*/
    for (i = 0; i < bufs->num; i++) {
        /* 最终调用的是内存池pool，开辟一段内存用作缓冲区，主要放置ngx_buf_t结构体
        Expands to:
        ngx_pcalloc(pool, sizeof(ngx_buf_t)) 
        */
        b = ngx_calloc_buf(pool);
        if (b == NULL) {
            return NULL;
        }

        /*
         * set by ngx_calloc_buf():
         *
         *     b->file_pos = 0;
         *     b->file_last = 0;
         *     b->file = NULL;
         *     b->shadow = NULL;
         *     b->tag = 0;
         *     and flags
         *
         */

        b->pos = p;
        b->last = p;
        b->temporary = 1;

        b->start = p;
        p += bufs->size;//p往前增
        b->end = p;
        /* 分配一个ngx_chain_t */
        cl = ngx_alloc_chain_link(pool);
        if (cl == NULL) {
            return NULL;
        }
        /* 将buf，都挂载到ngx_chain_t链表上，最终返回ngx_chain_t链表 */
        cl->buf = b;
        *ll = cl;//第一次运行到这儿时i==0  ll的地址是&chain   后续运行到这儿时  是最后一个链表节点&cl->next的内存位置 
        ll = &cl->next;
    }

    *ll = NULL;

    return chain;
}

/**
 * 将其它缓冲区链表放到已有缓冲区链表结构的尾部
 */
ngx_int_t
ngx_chain_add_copy(ngx_pool_t *pool, ngx_chain_t **chain, ngx_chain_t *in)
{
    ngx_chain_t  *cl, **ll;

    ll = chain;//chain 指向指针的指针，很绕
    /* 找到缓冲区链表结尾部分，cl->next== NULL；cl = *chain既为指针链表地址*/
    for (cl = *chain; cl; cl = cl->next) {
        ll = &cl->next;
    }

    while (in) {
        cl = ngx_alloc_chain_link(pool);
        if (cl == NULL) {
            *ll = NULL;
            return NGX_ERROR;
        }

        cl->buf = in->buf;//in上的buf拷贝到cl上面
        *ll = cl;//并且放到chain链表上最后一个节点的 NEXT节点上
        ll = &cl->next;//链表往下走  ll保存最新这个节点的next的地址
        in = in->next;//被拷贝的节点继续遍历，直到NULL
    }

    *ll = NULL;

    return NGX_OK;
}


/**
 * 从空闲的buf链表上，获取一个未使用的buf链表这段代码从名字上来看是获得一个free的buf，
 * 实际上是从一个free的链表中获得一个chain节点或者是重新分配一个chain节点
 *nginx中的链表操作很多都是头链表操作，即如果需要添加链表元素的话通常都将该元素添加到头上
 */
ngx_chain_t *
ngx_chain_get_free_buf(ngx_pool_t *p, ngx_chain_t **free)
{
    ngx_chain_t  *cl;
    /* 空闲列表中有数据，则直接返回 第一个空闲的节点*/
    if (*free) {
        cl = *free;//得到第一个空闲节点
        *free = cl->next;//空闲链表指向第二个空闲节点
        cl->next = NULL;//返回前置空next指针，这个指针对使用者没有用
        return cl;
    }
    /*没有找到空闲节点  那么分配一个新的链表节点 */
    cl = ngx_alloc_chain_link(p);
    if (cl == NULL) {
        return NULL;
    }
    /*申请一个buf节点  注意buf节点是哪个比较大 比较复杂的结构*/
    cl->buf = ngx_calloc_buf(p);
    if (cl->buf == NULL) {
        return NULL;
    }

    cl->next = NULL;

    return cl;
}

/*
需要处理的链表是out指针指向的链表，而free指向的应该就是当前存在的free链表，而busy链表则是当前存在的busy链表，该链表也是待处理的链表
所以开始的时候需要判断将out应该放到哪一个位置，如果busy当前就存在的话，那么就应该将out放置到busy的最后，如果当前busy链表不存在，那么处理就是
将其作为busy链表进行处理
而后面的操作则是说明从头对busy链表实行检查，如果busy链表中的buf还存在需要处理的内存空间，那么就需要停止处理，否则就将其置为空（即对last和pos进行处理）
*/
void
ngx_chain_update_chains(ngx_pool_t *p, ngx_chain_t **free, ngx_chain_t **busy,
    ngx_chain_t **out, ngx_buf_tag_t tag)
{
    ngx_chain_t  *cl;

    if (*out) {
        if (*busy == NULL) {
            *busy = *out;

        } else {
            for (cl = *busy; cl->next; cl = cl->next) { /* void */ }

            cl->next = *out;
        }

        *out = NULL;
    }

    while (*busy) {
        cl = *busy;

        if (ngx_buf_size(cl->buf) != 0) {
            break;
        }

        if (cl->buf->tag != tag) {
            *busy = cl->next;
            ngx_free_chain(p, cl);
            continue;
        }

        cl->buf->pos = cl->buf->start;
        cl->buf->last = cl->buf->start;

        *busy = cl->next;
        cl->next = *free;
        *free = cl;
    }
}


off_t
ngx_chain_coalesce_file(ngx_chain_t **in, off_t limit)
{
    off_t         total, size, aligned, fprev;
    ngx_fd_t      fd;
    ngx_chain_t  *cl;

    total = 0;

    cl = *in;
    fd = cl->buf->file->fd;

    do {
        size = cl->buf->file_last - cl->buf->file_pos;

        if (size > limit - total) {
            size = limit - total;

            aligned = (cl->buf->file_pos + size + ngx_pagesize - 1)
                       & ~((off_t) ngx_pagesize - 1);

            if (aligned <= cl->buf->file_last) {
                size = aligned - cl->buf->file_pos;
            }

            total += size;
            break;
        }

        total += size;
        fprev = cl->buf->file_pos + size;
        cl = cl->next;

    } while (cl
             && cl->buf->in_file
             && total < limit
             && fd == cl->buf->file->fd
             && fprev == cl->buf->file_pos);

    *in = cl;

    return total;
}


ngx_chain_t *
ngx_chain_update_sent(ngx_chain_t *in, off_t sent)
{
    off_t  size;

    for ( /* void */ ; in; in = in->next) {

        if (ngx_buf_special(in->buf)) {
            continue;
        }

        if (sent == 0) {
            break;
        }

        size = ngx_buf_size(in->buf);

        if (sent >= size) {
            sent -= size;

            if (ngx_buf_in_memory(in->buf)) {
                in->buf->pos = in->buf->last;
            }

            if (in->buf->in_file) {
                in->buf->file_pos = in->buf->file_last;
            }

            continue;
        }

        if (ngx_buf_in_memory(in->buf)) {
            in->buf->pos += (size_t) sent;
        }

        if (in->buf->in_file) {
            in->buf->file_pos += sent;
        }

        break;
    }

    return in;
}
