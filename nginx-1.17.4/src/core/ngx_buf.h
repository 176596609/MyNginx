
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_BUF_H_INCLUDED_
#define _NGX_BUF_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef void *            ngx_buf_tag_t;

typedef struct ngx_buf_s  ngx_buf_t;
/**
 * Nginx缓冲区
 * 1. 从上面这个数据结构中，可以看到ngx_buf_t结构，即可以处理内存，也可以处理文件。
 * 2. Nginx使用了位域的方法，节省存储空间。
 * 3. 每个buf都记录了开始和结束点以及未处理的开始和结束点，因为缓冲区的内存申请了之后，是可以被复用的。
 * 4. 所有缓冲区需要的数据结构以及缓冲区的buf内存块都会被分配到内存池上面。
 */
struct ngx_buf_s {
    //可以参考ngx_create_temp_buf         函数空间在ngx_create_temp_buf创建，让指针指向这些空间
/*pos通常是用来告诉使用者本次应该从pos这个位置开始处理内存中的数据，这样设置是因为同一个ngx_buf_t可能被多次反复处理。
当然，pos的含义是由使用它的模块定义的*/
    //它的pos成员和last成员指向的地址之间的内存就是接收到的还未解析的字符流
    u_char          *pos;/* 待处理数据的开始标记  */
    u_char          *last;/* 待处理数据的结尾标记   last通常表示有效的内容到此为止，注意，pos与last之间的内存是希望nginx处理的内容*/
    /* 处理文件时，file_pos与file_last的含义与处理内存时的pos与last相同，file_pos表示将要处理的文件位置，file_last表示截止的文件位置 */
    off_t            file_pos;/* 处理文件时，待处理的文件开始标记  */
    off_t            file_last;/* 处理文件时，待处理的文件结尾标记  */

    u_char          *start;         /* start of buffer 如果ngx_buf_t缓冲区用于内存，那么start指向这段内存的起始地址 */
    u_char          *end;           /* end of buffer 与start成员对应，指向缓冲区内存的末尾*/
    ngx_buf_tag_t    tag;/* 缓冲区标记地址，是一个void类型的指针。 使用者可以关联任意的对象上去，只要对使用者有意义。 表示当前缓冲区的类型，例如由哪个模块使用就指向这个模块ngx_module_t变量的地址*/
    ngx_file_t      *file;/* 引用的文件   用于存储接收到所有包体后，把包体内容写入到file文件中，赋值见ngx_http_read_client_request_body*/
    /*当前缓冲区的影子缓冲区，该成员很少用到，仅仅在使用缓冲区转发上游服务器的响应时才使用了shadow成员，这是因为Nginx太节
    约内存了，分配一块内存并使用ngx_buf_t表示接收到的上游服务器响应后，在向下游客户端转发时可能会把这块内存存储到文件中，也
    可能直接向下游发送，此时Nginx绝不会重新复制一份内存用于新的目的，而是再次建立一个ngx_buf_t结构体指向原内存，这样多个
    ngx_buf_t结构体指向了同一块内存，它们之间的关系就通过shadow成员来引用。这种设计过于复杂，通常不建议使用

    当这个buf完整copy了另外一个buf的所有字段的时候，那么这两个buf指向的实际上是同一块内存，或者是同一个文件的同一部分，此
    时这两个buf的shadow字段都是指向对方的。那么对于这样的两个buf，在释放的时候，就需要使用者特别小心，具体是由哪里释放，要
    提前考虑好，如果造成资源的多次释放，可能会造成程序崩溃！
    */
    ngx_buf_t       *shadow;


    /* the buf's content could be changed */
    unsigned         temporary:1; /*为1时表示该buf所包含的内容是在一个用户创建的内存块中，并且可以被在filter处理的过程中进行变更，而不会造成问题 临时内存标志位，为1时表示数据在内存中且这段内存可以修改 */

    /*
     * the buf's content is in a memory cache or in a read only memory
     * and must not be changed
     */
    unsigned         memory:1;/*为1时表示该buf所包含的内容是在内存中，但是这些内容确不能被进行处理的filter进行变更。  标志位，为1时，内存只读 */

    /* the buf's content is mmap()ed and must not be changed */
    unsigned         mmap:1;/* 标志位，为1时，mmap映射过来的内存，不可修改 */

    unsigned         recycled:1;/* 标志位，为1时，可回收 */
    unsigned         in_file:1;/*  ngx_buf_t有一个标志位in_file，将in_file置为1就表示这次ngx_buf_t缓冲区发送的是文件而不是内存。调用ngx_http_output_filter后，若Nginx检测到in_file为1，将会从ngx_buf_t缓冲区中的file成员处获取实际的文件。file的类型是ngx_file_t 标志位，为1时，表示处理的是文件 */
    unsigned         flush:1;/* 标志位，为1时，表示需要进行flush操作 */
    unsigned         sync:1;/* 标志位，为1时，表示可以进行同步操作，容易引起堵塞 */
    unsigned         last_buf:1;/* 数据被以多个chain传递给了过滤器，此字段为1表明这是最后一个chain */
    unsigned         last_in_chain:1;/* 标志位，表示是否是ngx_chain_t中的最后一块缓冲区 */

    unsigned         last_shadow:1;/* 标志位，为1时，表示是否是最后一个影子缓冲区 */
    unsigned         temp_file:1;/* 标志位，为1时，表示当前缓冲区是否属于临时文件 */

    /* STUB */ int   num;
};

/*
ngx_chain_t是与ngx_buf_t配合使用的链表数据结构，下面看一下它的定义：
typedef struct ngx_chain_s       ngx_chain_t;
struct ngx_chain_s {
    ngx_buf_t    *buf;
    ngx_chain_t  *next;
};

buf指向当前的ngx_buf_t缓冲区，next则用来指向下一个ngx_chain_t。如果这是最后一个ngx_chain_t，则需要把next置为NULL。

在向用户发送HTTP 包体时，就要传入ngx_chain_t链表对象，注意，如果是最后一个ngx_chain_t，那么必须将next置为NULL，
否则永远不会发送成功，而且这个请求将一直不会结束（Nginx框架的要求）。
*/
struct ngx_chain_s {
    ngx_buf_t    *buf;
    ngx_chain_t  *next;
};

//表示num个size空间大小  如 4 8K，表示4个8K空间，可以参考ngx_conf_set_bufs_slot
typedef struct {
    ngx_int_t    num;
    size_t       size;
} ngx_bufs_t;


typedef struct ngx_output_chain_ctx_s  ngx_output_chain_ctx_t;

typedef ngx_int_t (*ngx_output_chain_filter_pt)(void *ctx, ngx_chain_t *in);

typedef void (*ngx_output_chain_aio_pt)(ngx_output_chain_ctx_t *ctx,
    ngx_file_t *file);

struct ngx_output_chain_ctx_s {
     /* 保存临时的buf 实际buf指向的内存空间在ngx_output_chain_align_file_buf或者ngx_output_chain_get_buf 开辟的
     */ 
    ngx_buf_t                   *buf;
    ngx_chain_t                 *in;//in是待发送的数据，busy是已经调用ngx_chain_writer但还没有发送完毕。 实际in是在ngx_output_chain->ngx_output_chain_add_copy(ctx->pool, &ctx->in, in)让ctx->in是输入参数in的直接拷贝赋值
    ngx_chain_t                 *free;/* 保存了已经发送完毕的chain，以便于重复利用 */  
    ngx_chain_t                 *busy;/* 保存了还未发送的chain */  

    unsigned                     sendfile:1;
    unsigned                     directio:1;
    unsigned                     unaligned:1;
    unsigned                     need_in_memory:1;/* 是否需要在内存中保存一份(使用sendfile的话，内存中没有文件的拷贝的，而我们有时需要处理文件，此时就需要设置这个标记) */ 
    unsigned                     need_in_temp:1;/* 是否需要在内存中重新复制一份，不管buf是在内存还是文件, 这样的话，后续模块可以直接修改这块内存 */  
    unsigned                     aio:1;

#if (NGX_HAVE_FILE_AIO || NGX_COMPAT)
    ngx_output_chain_aio_pt      aio_handler;
#if (NGX_HAVE_AIO_SENDFILE || NGX_COMPAT)
    ssize_t                    (*aio_preload)(ngx_buf_t *file);
#endif
#endif

#if (NGX_THREADS || NGX_COMPAT)
    ngx_int_t                  (*thread_handler)(ngx_thread_task_t *task,
                                                 ngx_file_t *file);
    ngx_thread_task_t           *thread_task;
#endif

    off_t                        alignment;

    ngx_pool_t                  *pool;
    ngx_int_t                    allocated;
    ngx_bufs_t                   bufs;
    ngx_buf_tag_t                tag;

    ngx_output_chain_filter_pt   output_filter;
    void                        *filter_ctx;
};


typedef struct {
    ngx_chain_t                 *out;//还没有发送出去的待发送数据的头部
    ngx_chain_t                **last;//永远指向最后一个ngx_chain_t的next字段的地址。这样可以通过这个地址不断的在后面增加元素。
    ngx_connection_t            *connection;
    ngx_pool_t                  *pool;//等于request对应的pool，见ngx_http_upstream_init_request
    off_t                        limit;
} ngx_chain_writer_ctx_t;


#define NGX_CHAIN_ERROR     (ngx_chain_t *) NGX_ERROR

//返回这个buf里面的内容是否在内存里。
#define ngx_buf_in_memory(b)        (b->temporary || b->memory || b->mmap)
#define ngx_buf_in_memory_only(b)   (ngx_buf_in_memory(b) && !b->in_file)//是不是内存空间  可能是排除文件映射的情况 //返回这个buf里面的内容是否仅仅在内存里，并且没有在文件里。
//如果是special的buf，应该是从ngx_http_send_special过来的
//返回该buf是否是一个特殊的buf，只含有特殊的标志和没有包含真正的数据。
#define ngx_buf_special(b)                                                   \
    ((b->flush || b->last_buf || b->sync)                                    \
     && !ngx_buf_in_memory(b) && !b->in_file)
//返回该buf是否是一个只包含sync标志而不包含真正数据的特殊buf。
#define ngx_buf_sync_only(b)                                                 \
    (b->sync                                                                 \
     && !ngx_buf_in_memory(b) && !b->in_file && !b->flush && !b->last_buf)
//buf  b->last - b->pos待处理的数据区域  主要是处理tcp黏包   返回该buf所含数据的大小，不管这个数据是在文件里还是在内存里。
#define ngx_buf_size(b)                                                      \
    (ngx_buf_in_memory(b) ? (off_t) (b->last - b->pos):                      \
                            (b->file_last - b->file_pos))

ngx_buf_t *ngx_create_temp_buf(ngx_pool_t *pool, size_t size);
ngx_chain_t *ngx_create_chain_of_bufs(ngx_pool_t *pool, ngx_bufs_t *bufs);


#define ngx_alloc_buf(pool)  ngx_palloc(pool, sizeof(ngx_buf_t))
#define ngx_calloc_buf(pool) ngx_pcalloc(pool, sizeof(ngx_buf_t))

ngx_chain_t *ngx_alloc_chain_link(ngx_pool_t *pool);
//直接交还给Nginx内存池的pool->chain空闲buf链表
#define ngx_free_chain(pool, cl)                                             \
    cl->next = pool->chain;                                                  \
    pool->chain = cl



ngx_int_t ngx_output_chain(ngx_output_chain_ctx_t *ctx, ngx_chain_t *in);
ngx_int_t ngx_chain_writer(void *ctx, ngx_chain_t *in);

ngx_int_t ngx_chain_add_copy(ngx_pool_t *pool, ngx_chain_t **chain,
    ngx_chain_t *in);
ngx_chain_t *ngx_chain_get_free_buf(ngx_pool_t *p, ngx_chain_t **free);
void ngx_chain_update_chains(ngx_pool_t *p, ngx_chain_t **free,
    ngx_chain_t **busy, ngx_chain_t **out, ngx_buf_tag_t tag);

off_t ngx_chain_coalesce_file(ngx_chain_t **in, off_t limit);

ngx_chain_t *ngx_chain_update_sent(ngx_chain_t *in, off_t sent);

#endif /* _NGX_BUF_H_INCLUDED_ */
