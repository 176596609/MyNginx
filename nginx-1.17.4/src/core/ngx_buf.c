
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
	 * 被清空的ngx_chain_t结构都会放在pool->chain 缓冲链上  被释放的ngx_chain_t是通过ngx_free_chain添加到poll->chain上的
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
将out链表插入到busy链表尾部，同时将合并后的链表从头开始的所有没有使用的节点，插入到空闲链表。
合并链表后，out为null

因为nginx可以提前flush输出，所以这些buf被输出后就可以重复使用，可以避免重分配，提高系统性能，被称为free_buf，而没有被输出的
buf就是busy_buf。nginx没有特别的集成这个特性到自身，但是提供了一个函数ngx_chain_update_chains来帮助开发者维护这两个缓冲区队列

该函数功能就是把新读到的out数据添加到busy表尾部，然后把busy表中已经处理完毕的buf节点从busy表中摘除，然后放到free表头部
未发送出去的buf节点既会在out链表中，也会在busy链表中
*/
void
ngx_chain_update_chains(ngx_pool_t *p, ngx_chain_t **free, ngx_chain_t **busy,
    ngx_chain_t **out, ngx_buf_tag_t tag)
{
    ngx_chain_t  *cl;

    if (*out) {//如果*out 不是NULL  
        if (*busy == NULL) {//如果*busy==NULL
            *busy = *out;  //*busy指向和 *out同样的链表

        } else {//如果*busy不为空，那么找到*busy的最后一个节点  遍历直到cl指向最后一个节点
            for (cl = *busy; cl->next; cl = cl->next) { /* void */ }

            cl->next = *out;//把整个*out链表追加到cl节点后面  换句话说*out链表成为了*busy链表的一部分
        }

        *out = NULL;
    }

    while (*busy) {//遍历整个*busy链表
        cl = *busy;
        //合并后的该busy链表节点有内容时，则表示剩余节点都有内容，则退出  ？？？其实没有读懂这  ngx_buf_size看起来是表示节点上还有多少空间可以利用
        if (ngx_buf_size(cl->buf) != 0) {//如果还有人正在使用这个节点buf  那么跳出循环   pos==last意味着已经处理完了
            break;
        }
        
        if (cl->buf->tag != tag) { //缓冲区类型不同，直接释放   tag 中存储的是 函数指
            *busy = cl->next;
            ngx_free_chain(p, cl);//节点摘下来  节点转换到pool->chain链表上
            continue;
        }
        //把该空间的pos last都指向start开始处，表示该ngx_buf_t没有数据在里面，因此可以把他加到free表中，可以继续读取数据到free中的ngx_buf_t节点了
        cl->buf->pos = cl->buf->start;
        cl->buf->last = cl->buf->start;
        //继续遍历
        *busy = cl->next;
        //将该空闲空闲区加入到free链表表头
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


//计算本次掉用ngx_writev发送出去的send字节在in链表中所有数据的那个位置 in链表入口  sent是发送了的字节数
ngx_chain_t *
ngx_chain_update_sent(ngx_chain_t *in, off_t sent)
{
    off_t  size;

    for ( /* void */ ; in; in = in->next) {

        if (ngx_buf_special(in->buf)) {//猜测只有发送的链表才处理 否则continue
            continue;
        }

        if (sent == 0) {//已经找到发送的末尾了
            break;
        }

        size = ngx_buf_size(in->buf);//当前链表节点还没有发送的大小

        if (sent >= size) {//说明该in->buf数据已经全部发送出去
            sent -= size;//标记后面还有多少数据是我发送过的

            if (ngx_buf_in_memory(in->buf)) {//说明该in->buf数据已经全部发送出去
                in->buf->pos = in->buf->last;//清空这段内存。继续找下一个
            }

            if (in->buf->in_file) {//如果是文件 那么清空文件这个节点
                in->buf->file_pos = in->buf->file_last;
            }

            continue;
        }
        //说明这个节点没有完全发送结束
        if (ngx_buf_in_memory(in->buf)) {
            in->buf->pos += (size_t) sent;//下一字节数据在in->buf->pos+send位置，下次从这个位置开始发送
        }

        if (in->buf->in_file) {
            in->buf->file_pos += sent;
        }

        break;
    }

    return in;
}
