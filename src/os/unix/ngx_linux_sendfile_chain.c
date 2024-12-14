
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>

static ssize_t ngx_linux_sendfile(ngx_connection_t *c, ngx_buf_t *file,
                                  size_t size);

#if (NGX_THREADS)
#include <ngx_thread_pool.h>

#if !(NGX_HAVE_SENDFILE64)
#error sendfile64() is required!
#endif

static ssize_t ngx_linux_sendfile_thread(ngx_connection_t *c, ngx_buf_t *file,
    size_t size);
static void ngx_linux_sendfile_thread_handler(void *data, ngx_log_t *log);
#endif


/*
 * On Linux up to 2.4.21 sendfile() (syscall #187) works with 32-bit
 * offsets only, and the including <sys/sendfile.h> breaks the compiling,
 * if off_t is 64 bit wide.  So we use own sendfile() definition, where offset
 * parameter is int32_t, and use sendfile() for the file parts below 2G only,
 * see src/os/unix/ngx_linux_config.h
 *
 * Linux 2.4.21 has the new sendfile64() syscall #239.
 *
 * On Linux up to 2.6.16 sendfile() does not allow to pass the count parameter
 * more than 2G-1 bytes even on 64-bit platforms: it returns EINVAL,
 * so we limit it to 2G-1 bytes.
 *
 * On Linux 2.6.16 and later, sendfile() silently limits the count parameter
 * to 2G minus the page size, even on 64-bit platforms.
 */

#define NGX_SENDFILE_MAXSIZE  2147483647L

/*
向后端的数据发送,不会经过各个filter模块,向客户端的包体响应会经过各个filter模块
2016/01/05 21:02:43[           ngx_event_process_posted,    67]  [debug] 23495#23495: *1 delete posted event AEA04098
2016/01/05 21:02:43[          ngx_http_upstream_handler,  1400]  [debug] 23495#23495: *1 http upstream request(ev->write:1): "/test2.php?"
2016/01/05 21:02:43[ngx_http_upstream_send_request_handler,  2420]  [debug] 23495#23495: *1 http upstream send request handler
2016/01/05 21:02:43[     ngx_http_upstream_send_request,  2167]  [debug] 23495#23495: *1 http upstream send request
2016/01/05 21:02:43[ngx_http_upstream_send_request_body,  2305]  [debug] 23495#23495: *1 http upstream send request body
2016/01/05 21:02:43[                   ngx_output_chain,    67][yangya  [debug] 23495#23495: *1 ctx->sendfile:0, ctx->aio:0, ctx->directio:0
2016/01/05 21:02:43[                   ngx_output_chain,    90][yangya  [debug] 23495#23495: *1 only one chain buf to output_filter
2016/01/05 21:02:43[                   ngx_chain_writer,   747]  [debug] 23495#23495: *1 chain writer buf fl:0 s:600
2016/01/05 21:02:43[                   ngx_chain_writer,   762]  [debug] 23495#23495: *1 chain writer in: 080F268C
2016/01/05 21:02:43[           ngx_linux_sendfile_chain,   161][yangya  [debug] 23495#23495: *1 @@@@@@@@@@@@@@@@@@@@@@@begin ngx_linux_sendfile_chain @@@@@@@@@@@@@@@@@@@
2016/01/05 21:02:43[                         ngx_writev,   201]  [debug] 23495#23495: *1 writev: 600 of 600
2016/01/05 21:02:43[                   ngx_chain_writer,   801]  [debug] 23495#23495: *1 chain writer out: 00000000
向后端的数据发送,不会经过各个filter模块,向客户端的包体响应会经过各个filter模块
2016/01/05 21:02:43[ ngx_event_pipe_write_to_downstream,   623]  [debug] 23495#23495: *1 pipe write downstream flush out
2016/01/05 21:02:43[             ngx_http_output_filter,  3338]  [debug] 23495#23495: *1 http output filter "/test2.php?"
2016/01/05 21:02:43[               ngx_http_copy_filter,   199]  [debug] 23495#23495: *1 http copy filter: "/test2.php?", r->aio:0
2016/01/05 21:02:43[                   ngx_output_chain,    67][yangya  [debug] 23495#23495: *1 ctx->sendfile:0, ctx->aio:0, ctx->directio:0
2016/01/05 21:02:43[                      ngx_read_file,    83]  [debug] 23495#23495: *1 read file /var/yyz/cache_xxx/temp/1/00/0000000001: 15, 081109E0, 215, 206
2016/01/05 21:02:43[           ngx_http_postpone_filter,   176]  [debug] 23495#23495: *1 http postpone filter "/test2.php?" 080F2D4C
2016/01/05 21:02:43[       ngx_http_chunked_body_filter,   212]  [debug] 23495#23495: *1 http chunk: 215
2016/01/05 21:02:43[       ngx_http_chunked_body_filter,   273]  [debug] 23495#23495: *1 yang test ..........xxxxxxxx ################## lstbuf:0
2016/01/05 21:02:43[              ngx_http_write_filter,   151]  [debug] 23495#23495: *1 write old buf t:1 f:0 080F2AE8, pos 080F2AE8, size: 180 file: 0, size: 0
2016/01/05 21:02:43[              ngx_http_write_filter,   207]  [debug] 23495#23495: *1 write new buf t:1 f:0 080F2D98, pos 080F2D98, size: 4 file: 0, size: 0
2016/01/05 21:02:43[              ngx_http_write_filter,   207]  [debug] 23495#23495: *1 write new buf t:1 f:0 081109E0, pos 081109E0, size: 215 file: 0, size: 0
2016/01/05 21:02:43[              ngx_http_write_filter,   207]  [debug] 23495#23495: *1 write new buf t:0 f:0 00000000, pos 080CDEDD, size: 2 file: 0, size: 0
2016/01/05 21:02:43[              ngx_http_write_filter,   247]  [debug] 23495#23495: *1 http write filter: last:0 flush:1 size:401
2016/01/05 21:02:43[              ngx_http_write_filter,   379]  [debug] 23495#23495: *1 http write filter limit 0
2016/01/05 21:02:43[           ngx_linux_sendfile_chain,   161][yangya  [debug] 23495#23495: *1 @@@@@@@@@@@@@@@@@@@@@@@begin ngx_linux_sendfile_chain @@@@@@@@@@@@@@@@@@@
2016/01/05 21:02:43[                         ngx_writev,   201]  [debug] 23495#23495: *1 writev: 401 of 401
2016/01/05 21:02:43[              ngx_http_write_filter,   385]  [debug] 23495#23495: *1 http write filter 00000000
2016/01/05 21:02:43[               ngx_http_copy_filter,   276]  [debug] 23495#23495: *1 http copy filter rc: 0, buffered:0 "/test2.php?"
2016/01/05 21:02:43[ ngx_event_pipe_write_to_downstream,   662]  [debug] 23495#23495: *1 pipe write downstream done
*/

//ngx_linux_io
ngx_chain_t * //只要支持sendfile,不管有没有配置sendfile on都会走到该函数中,除非开启了异步aio
ngx_linux_sendfile_chain(ngx_connection_t *c, ngx_chain_t *in, off_t limit) { //向后端的数据发送,不会经过各个filter模块,向客户端的包体响应会经过各个filter模块
    int tcp_nodelay;
    off_t send, prev_send;
    size_t file_size, sent;
    ssize_t n;
    ngx_err_t err;
    ngx_buf_t *file;
    ngx_event_t *wev;
    ngx_chain_t *cl;
    ngx_iovec_t header;
    struct iovec headers[NGX_IOVS_PREALLOCATE];

    wev = c->write;

    if (!wev->ready) {
        return in;
    }


    /* the maximum limit size is 2G-1 - the page size */

    if (limit == 0 || limit > (off_t) (NGX_SENDFILE_MAXSIZE - ngx_pagesize)) {
        limit = NGX_SENDFILE_MAXSIZE - ngx_pagesize;
    }


    send = 0;

    header.iovs = headers;
    header.nalloc = NGX_IOVS_PREALLOCATE;

    for (;;) {
        prev_send = send;

        /* create the iovec and coalesce the neighbouring bufs */
        //把in链中的buf拷贝到vec->iovs[n++]中,注意只会拷贝内存中的数据到iovec中,不会拷贝文件中的
        cl = ngx_output_chain_to_iovec(&header, in, limit - send, c->log);

        if (cl == NGX_CHAIN_ERROR) {
            return NGX_CHAIN_ERROR;
        }

        send += header.size; //in中所有数据size之和

        /* set TCP_CORK if there is a header before a file */

        if (c->tcp_nopush == NGX_TCP_NOPUSH_UNSET
            && header.count != 0 //等于0,则表明chain链中的所有数据在文件中
            && cl
            && cl->buf->in_file) {
            /* the TCP_CORK and TCP_NODELAY are mutually exclusive */

            if (c->tcp_nodelay == NGX_TCP_NODELAY_SET) {

                tcp_nodelay = 0;

                if (setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY,
                               (const void *) &tcp_nodelay, sizeof(int)) == -1) {
                    err = ngx_socket_errno;

                    /*
                     * there is a tiny chance to be interrupted, however,
                     * we continue a processing with the TCP_NODELAY
                     * and without the TCP_CORK
                     */

                    if (err != NGX_EINTR) {
                        wev->error = 1;
                        ngx_connection_error(c, err,
                                             "setsockopt(TCP_NODELAY) failed");
                        return NGX_CHAIN_ERROR;
                    }

                } else {
                    c->tcp_nodelay = NGX_TCP_NODELAY_UNSET;

                    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, c->log, 0,
                                   "no tcp_nodelay");
                }
            }

            if (c->tcp_nodelay == NGX_TCP_NODELAY_UNSET) {

                if (ngx_tcp_nopush(c->fd) == -1) {
                    err = ngx_socket_errno;

                    /*
                     * there is a tiny chance to be interrupted, however,
                     * we continue a processing without the TCP_CORK
                     */

                    if (err != NGX_EINTR) {
                        wev->error = 1;
                        ngx_connection_error(c, err,
                                             ngx_tcp_nopush_n " failed");
                        return NGX_CHAIN_ERROR;
                    }

                } else {
                    c->tcp_nopush = NGX_TCP_NOPUSH_SET;

                    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, c->log, 0,
                                   "tcp_nopush");
                }
            }
        }

        /* get the file buf */
        //等于0,则表明chain链中的所有数据在文件中,一般sendfile on的时候走这里
        /*说明chain中的数据是in_file的,也就是在缓存文件中,一般开启sendfile on的时候走这里,因为ngx_output_chain_as_is返回1,不会重新开辟内存空间读取缓存内容.
         in_file中的内存还是in_file的,而不会拷贝到新分配的内存中, 参考ngx_http_copy_filter->ngx_output_chain   ngx_output_chain_as_is等*/
        if (header.count == 0 && cl && cl->buf->in_file && send < limit) {
            file = cl->buf;

            /* coalesce the neighbouring file bufs */

            file_size = (size_t) ngx_chain_coalesce_file(&cl, limit - send);

            send += file_size;
#if 1
            if (file_size == 0) {
                ngx_debug_point();
                return NGX_CHAIN_ERROR;
            }
#endif

            n = ngx_linux_sendfile(c, file, file_size);

            if (n == NGX_ERROR) {
                return NGX_CHAIN_ERROR;
            }

            if (n == NGX_DONE) {
                /* thread task posted */
                return in;
            }

            sent = (n == NGX_AGAIN) ? 0 : n;

        } else {
            /*说明chain中的数据在内存中,一般不开启sendfile on的时候走这里,因为ngx_http_copy_filter->ngx_output_chain中会重新
        分配内存读取缓存文件内容,见ngx_output_chain_as_is.之前buf->in_file的内容就会变好内存型的*/
            n = ngx_writev(c, &header);

            if (n == NGX_ERROR) {
                return NGX_CHAIN_ERROR;
            }

            sent = (n == NGX_AGAIN) ? 0 : n;
        }

        c->sent += sent;

        in = ngx_chain_update_sent(in, sent);

        if (n == NGX_AGAIN) {
            wev->ready = 0;
            return in;
        }

        if ((size_t) (send - prev_send) != sent) {

            /*
             * sendfile() on Linux 4.3+ might be interrupted at any time,
             * and provides no indication if it was interrupted or not,
             * so we have to retry till an explicit EAGAIN
             *
             * sendfile() in threads can also report less bytes written
             * than we are prepared to send now, since it was started in
             * some point in the past, so we again have to retry
             */

            send = prev_send + sent;
        }

        if (send >= limit || in == NULL) {
            return in;
        }
    }
}

/*
rocktmq中对零拷贝的解释
(1)零拷贝原理:Consumer消费消息过程,使用了零拷贝,零拷贝包括一下2中方式,RocketMQ使用第一种方式,因小块数据传输的要求效果比sendfile方式好
    a )使用mmap+write方式   (mmap将一个文件或者其它对象映射进内存)
     优点:即使频繁调用,使用小文件块传输,效率也很高
     缺点:不能很好的利用DMA方式,会比sendfile多消耗CPU资源,内存安全性控制复杂,需要避免JVM Crash问题
    b)使用sendfile方式
     优点:可以利用DMA方式,消耗CPU资源少,大块文件传输效率高,无内存安全新问题
     缺点:小块文件效率低于mmap方式,只能是BIO方式传输,不能使用NIO
    mmap是一种内存映射文件的方法,即将一个文件或者其它对象映射到进程的地址空间,实现文件磁盘地址和进程虚拟地址空间
中一段虚拟地址的一一对映关系.实现这样的映射关系后,进程就可以采用指针的方式读写操作这一段内存,而系统会自动回
写脏页面到对应的文件磁盘上,即完成了对文件的操作而不必再调用read,write等系统调用函数.相反,内核空间对这段区域
的修改也直接反映用户空间,从而可以实现不同进程间的文件共享.
http://www.linuxjournal.com/article/6345?page=0,0
http://blog.csdn.net/kisimple/article/details/42499225
*/
static ssize_t
ngx_linux_sendfile(ngx_connection_t *c, ngx_buf_t *file, size_t size) {
#if (NGX_HAVE_SENDFILE64)
    off_t offset;
#else
    int32_t    offset;
#endif
    ssize_t n;
    ngx_err_t err;

#if (NGX_THREADS)

    if (file->file->thread_handler) {
        return ngx_linux_sendfile_thread(c, file, size);
    }

#endif

#if (NGX_HAVE_SENDFILE64)
    offset = file->file_pos;
#else
    offset = (int32_t) file->file_pos;
#endif

    eintr:

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, c->log, 0,
                   "sendfile: @%O %uz", file->file_pos, size);
    //一般大缓存文件用aio发送,小文件用sendfile,因为aio是异步的,不影响其他流程,但是sendfile是同步的,太大的话可能需要多次sendfile才能发送完,有种阻塞感觉

    //它减少了内核态与用户态之间的两次内存复制,这样就会从磁盘中读取文件后直接在内核态发送到网卡设备,
    n = sendfile(c->fd, file->file->fd, &offset, size);

    if (n == -1) {
        err = ngx_errno;

        switch (err) {
            case NGX_EAGAIN:
                ngx_log_debug0(NGX_LOG_DEBUG_EVENT, c->log, err,
                               "sendfile() is not ready");
                return NGX_AGAIN;

            case NGX_EINTR:
                ngx_log_debug0(NGX_LOG_DEBUG_EVENT, c->log, err,
                               "sendfile() was interrupted");
                goto eintr;

            default:
                c->write->error = 1;
                ngx_connection_error(c, err, "sendfile() failed");
                return NGX_ERROR;
        }
    }

    if (n == 0) {
        /*
         * if sendfile returns zero, then someone has truncated the file,
         * so the offset became beyond the end of the file
         */

        ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                      "sendfile() reported that \"%s\" was truncated at %O",
                      file->file->name.data, file->file_pos);

        return NGX_ERROR;
    }

    ngx_log_debug3(NGX_LOG_DEBUG_EVENT, c->log, 0, "sendfile: %z of %uz @%O",
                   n, size, file->file_pos);

    return n;
}


#if (NGX_THREADS)
//ngx_linux_sendfile_thread中创建空间和赋值
typedef struct {
    ngx_buf_t     *file;
    ngx_socket_t   socket;
    size_t         size;

    size_t         sent;
    ngx_err_t      err;
} ngx_linux_sendfile_ctx_t;


static ssize_t
ngx_linux_sendfile_thread(ngx_connection_t *c, ngx_buf_t *file, size_t size)
{
    ngx_event_t               *wev;
    ngx_thread_task_t         *task;
    ngx_linux_sendfile_ctx_t  *ctx;

    ngx_log_debug3(NGX_LOG_DEBUG_CORE, c->log, 0,
                   "linux sendfile thread: %d, %uz, %O",
                   file->file->fd, size, file->file_pos);

    task = c->sendfile_task;

    if (task == NULL) {
        task = ngx_thread_task_alloc(c->pool, sizeof(ngx_linux_sendfile_ctx_t));
        if (task == NULL) {
            return NGX_ERROR;
        }

        task->handler = ngx_linux_sendfile_thread_handler;

        c->sendfile_task = task;
    }

    ctx = task->ctx;
    wev = c->write;

    if (task->event.complete) {
        task->event.complete = 0;

        if (ctx->err == NGX_EAGAIN) {
            /*
             * if wev->complete is set, this means that a write event
             * happened while we were waiting for the thread task, so
             * we have to retry sending even on EAGAIN
             */

            if (wev->complete) {
                return 0;
            }

            return NGX_AGAIN;
        }

        if (ctx->err) {
            wev->error = 1;
            ngx_connection_error(c, ctx->err, "sendfile() failed");
            return NGX_ERROR;
        }

        if (ctx->sent == 0) {
            /*
             * if sendfile returns zero, then someone has truncated the file,
             * so the offset became beyond the end of the file
             */

            ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                          "sendfile() reported that \"%s\" was truncated at %O",
                          file->file->name.data, file->file_pos);

            return NGX_ERROR;
        }

        return ctx->sent;
    }

    ctx->file = file;
    ctx->socket = c->fd;
    ctx->size = size;

    wev->complete = 0;

    if (file->file->thread_handler(task, file->file) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_DONE;
}


static void
ngx_linux_sendfile_thread_handler(void *data, ngx_log_t *log)
{
    ngx_linux_sendfile_ctx_t *ctx = data;

    off_t       offset;
    ssize_t     n;
    ngx_buf_t  *file;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, log, 0, "linux sendfile thread handler");

    file = ctx->file;
    offset = file->file_pos;

again:

    n = sendfile(ctx->socket, file->file->fd, &offset, ctx->size);

    if (n == -1) {
        ctx->err = ngx_errno;

    } else {
        ctx->sent = n;
        ctx->err = 0;
    }

#if 0
    ngx_time_update();
#endif

    ngx_log_debug4(NGX_LOG_DEBUG_EVENT, log, 0,
                   "sendfile: %z (err: %d) of %uz @%O",
                   n, ctx->err, ctx->size, file->file_pos);

    if (ctx->err == NGX_EINTR) {
        goto again;
    }
}

#endif /* NGX_THREADS */
