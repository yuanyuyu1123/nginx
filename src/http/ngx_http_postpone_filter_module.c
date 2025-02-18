
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


static ngx_int_t ngx_http_postpone_filter_add(ngx_http_request_t *r,
                                              ngx_chain_t *in);

static ngx_int_t ngx_http_postpone_filter_in_memory(ngx_http_request_t *r,
                                                    ngx_chain_t *in);

static ngx_int_t ngx_http_postpone_filter_init(ngx_conf_t *cf);


static ngx_http_module_t ngx_http_postpone_filter_module_ctx = {
        NULL,                                  /* preconfiguration */
        ngx_http_postpone_filter_init,         /* postconfiguration */

        NULL,                                  /* create main configuration */
        NULL,                                  /* init main configuration */

        NULL,                                  /* create server configuration */
        NULL,                                  /* merge server configuration */

        NULL,                                  /* create location configuration */
        NULL                                   /* merge location configuration */
};

ngx_module_t ngx_http_postpone_filter_module = {
        /*该模块实际上是为subrequest功能建立的,主要用户保证到后端多个节点的应答有序的发送到客户端,保证是我们期待的顺序*/
        NGX_MODULE_V1,
        &ngx_http_postpone_filter_module_ctx,  /* module context */
        NULL,                                  /* module directives */
        NGX_HTTP_MODULE,                       /* module type */
        NULL,                                  /* init master */
        NULL,                                  /* init module */
        NULL,                                  /* init process */
        NULL,                                  /* init thread */
        NULL,                                  /* exit thread */
        NULL,                                  /* exit process */
        NULL,                                  /* exit master */
        NGX_MODULE_V1_PADDING
};


static ngx_http_output_body_filter_pt ngx_http_next_body_filter;



/*sub1_r和sub2_r都是同一个父请求,就是root_r请求,sub1_r和sub2_r就是ngx_http_postponed_request_s->request成员
    它们由ngx_http_postponed_request_s->next连接在一起,参考ngx_http_subrequest

                      -----root_r(主请求)
                      |postponed
                      |                next
        -------------sub1_r(data1)--------------sub2_r(data1)
        |                                       |postponed
        |postponed                              |
        |                                     sub21_r-----sub22
        |
        |               next
      sub11_r(data11)-----------sub12_r(data12)
    图中的root节点即为主请求,它的postponed链表从左至右挂载了3个节点,SUB1是它的第一个子请求,DATA1是它产生的一段数据,SUB2是它的第2个子请求,
而且这2个子请求分别有它们自己的子请求及数据.ngx_connection_t中的data字段保存的是当前可以往out chain发送数据的请求,文章开头说到发到客户端
的数据必须按照子请求创建的顺序发送,这里即是按后续遍历的方法(SUB11->DATA11->SUB12->DATA12->(SUB1)->DATA1->SUB21->SUB22->(SUB2)->(ROOT)),
上图中当前能够往客户端(out chain)发送数据的请求显然就是SUB11,如果SUB12提前执行完成,并产生数据DATA121,只要前面它还有节点未发送完毕,
DATA121只能先挂载在SUB12的postponed链表下.这里还要注意一下的是c->data的设置,当SUB11执行完并且发送完数据之后,下一个将要发送的节点应该是
DATA11,但是该节点实际上保存的是数据,而不是子请求,所以c->data这时应该指向的是拥有改数据节点的SUB1请求.
发送数据到客户端优先级:
1.子请求优先级比父请求高
2.同级(一个r产生多个子请求)请求,从左到右优先级由高到低(因为先创建的子请求先发送数据到客户端)
发送数据到客户端顺序控制见ngx_http_postpone_filter       nginx通过子请求发送数据到后端见ngx_http_run_posted_requests  */

/* 当需要从后端多个服务器获取信息的时候,就需要等后端的
所有响应都收到后,并对这些应答进行合适的顺序才能发往客户端,才能保证有序的发送给客户端*/


//这里的参数in就是将要发送给客户端的一段包体
static ngx_int_t //subrequest注意ngx_http_run_posted_requests与ngx_http_subrequest ngx_http_postpone_filter ngx_http_finalize_request配合阅读
ngx_http_postpone_filter(ngx_http_request_t *r, ngx_chain_t *in) { //一般子请求接收到后端数据后不会立即发送到客户端,而是通过ngx_http_finalize_request->ngx_http_set_write_handler触发发送后端的数据
    ngx_connection_t *c;
    ngx_http_postponed_request_t *pr;
    //c是Nginx与下游客户端间的连接,c->data保存的是原始请求
    c = r->connection;

    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http postpone filter \"%V?%V\" %p", &r->uri, &r->args, in);

    if (r->subrequest_in_memory) {
        return ngx_http_postpone_filter_in_memory(r, in);
    }
    /*sub1_r和sub2_r都是同一个父请求,就是root_r请求,sub1_r和sub2_r就是ngx_http_postponed_request_s->request成员
     它们由ngx_http_postponed_request_s->next连接在一起,参考ngx_http_subrequest
                  -----root_r(主请求)
                         |postponed
                         |                next
           -------------sub1_r(data1)--------------sub2_r(data1)
           |                                       |postponed
           |postponed                              |
           |                                     sub21_r-----sub22
           |
           |               next
         sub11_r(data11)-----------sub12_r(data12)
    */
    //如果当前请求r是一个子请求(因为c->data指向原始请求),如上图,c->data指向sub11_r,如果r为sub12_r或者sub1_r或者sub2_r
    if (r != c->data) { //参考ngx_http_subrequest中的c->data = sr  ngx_connection_t中的data字段保存的是当前可以往out chain发送数据的请求,也就是优先级最高的请求
        /* 当前请求不能往out chain发送数据,如果产生了数据,新建一个节点,
       将它保存在当前请求的postponed队尾.这样就保证了数据按序发到客户端 */
        if (in) {
            /*如果待发送的in包体不为空,则把in加到postponed链表中属于当前请求的ngx_http_postponed_request_t结构体的out链表中,
              同时返回NGX_OK,这意味着本次不会把in包体发给客户瑞*/
            if (ngx_http_postpone_filter_add(r, in) != NGX_OK) { //把该子请求r上面读取到的数据挂接到该r->postponed上
                return NGX_ERROR;
            }

            return NGX_OK;
        }

#if 0
        /* TODO: SSI may pass NULL */
        ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                      "http postpone filter NULL inactive request");
#endif
        //如果当前请求是子请求,而in包体又为空,那么直接返回即可
        return NGX_OK;
    }
    /* 到这里,表示当前请求可以往out chain发送数据,如果它的postponed链表中没有子请求,也没有数据,
       则直接发送当前产生的数据in或者继续发送out chain中之前没有发送完成的数据 */

    //如果postponed为空,表示请求r没有子请求产生的响应需要转发
    if (r->postponed == NULL) {
        //直接调用下一个HTTP过滤模块继续处理in包体即可.如果没有错误的话,就会开始向下游客户端发送响应
        if (in || c->buffered) {
            return ngx_http_next_body_filter(r->main, in);
        }

        return NGX_OK;
    }
    /* r->postponed != NULL当前请求的postponed链表中之前就存在需要处理的节点,则新建一个节点,保存当前产生的数据in,并将它插入到postponed队尾 */

    //至此,说明postponed链表中是有子请求产生的响应需要转发的,可以先把in包体加到待转发响应
    if (in) {
        if (ngx_http_postpone_filter_add(r, in) != NGX_OK) {
            return NGX_ERROR;
        }
    }
    //循环处理postponed链表中所有子请求待转发的包  /* 处理postponed链表中的节点 */
    do {
        pr = r->postponed;
        /* 如果该节点保存的是一个子请求,则将它加到主请求的posted_requests链表中,以便下次调用ngx_http_run_posted_requests函数,处理该子节点 */

        //如果pr->request是子请求,则加入到原始请求的posted_requests队列中,等待HTTP框架下次调用这个请求时再来处理(参见11.7节)
        if (pr->request) { //说明pr节点代表的是子请求而不是数据

            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0,
                           "http postpone filter wake \"%V?%V\"",
                           &pr->request->uri, &pr->request->args);

            r->postponed = pr->next;
            /* 按照后续遍历产生的序列,因为当前请求(节点)有未处理的子请求(节点),必须先处理完改子请求,才能继续处理后面的子节点.
              这里将该子请求设置为可以往out chain发送数据的请求*/
            c->data = pr->request;
            //为该子请求创建ngx_http_posted_request_t添加到最上层root所处r的posted_requests中
            return ngx_http_post_request(pr->request, NULL);  /* 将该子请求加入主请求的posted_requests链表 */
            /*如果优先级低的子请求的数据线到达,则先通过ngx_http_postpone_filter_add缓存到r->postpone,然后r添加到pr->request->posted_requests,最后在高优先级请求后端
            数据到来后,会把之前缓存起来的低优先级请求的数据也一起在ngx_http_run_posted_requests中触发发送,从而保证真正发送到客户端数据时按照子请求优先级顺序发送的*/
        }
        /* 如果该节点保存的是数据,可以直接处理该节点,将它发送到out chain */

        //调用下一个HTTP过滤模块转发out链表中保存的待转发的包体
        if (pr->out == NULL) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                          "http postpone filter NULL output");

        } else {
            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0,
                           "http postpone filter output \"%V?%V\"",
                           &r->uri, &r->args);

            if (ngx_http_next_body_filter(r->main, pr->out) == NGX_ERROR) {
                return NGX_ERROR;
            }
        }

        r->postponed = pr->next; //遍历完postponed链表

    } while (r->postponed);

    return NGX_OK;
}


static ngx_int_t
ngx_http_postpone_filter_add(ngx_http_request_t *r, ngx_chain_t *in) { //postponed添加在ngx_http_postpone_filter_add ,删除在ngx_http_finalize_request
    ngx_http_postponed_request_t *pr, **ppr;

    if (r->postponed) {
        for (pr = r->postponed; pr->next; pr = pr->next) { /* void */ }

        if (pr->request == NULL) { //说明最后一句ngx_http_postponed_request_t节点只是简单的存储in链数据,则直接使用该结构即可
            goto found;
        }

        ppr = &pr->next;

    } else {
        ppr = &r->postponed;
    }

    pr = ngx_palloc(r->pool, sizeof(ngx_http_postponed_request_t));
    if (pr == NULL) {
        return NGX_ERROR;
    }

    *ppr = pr; //把新创建的pr添加到r->postponed尾部

    pr->request = NULL; //in数据通过创建新的ngx_http_postponed_request_t添加到r->postponed中,但是这时候新创建的pr节点的request为NULL
    pr->out = NULL;
    pr->next = NULL;

    found:

    if (ngx_chain_add_copy(r->pool, &pr->out, in) == NGX_OK) {
        return NGX_OK;
    }

    return NGX_ERROR;
}


static ngx_int_t
ngx_http_postpone_filter_in_memory(ngx_http_request_t *r, ngx_chain_t *in) {
    size_t len;
    ngx_buf_t *b;
    ngx_connection_t *c;
    ngx_http_core_loc_conf_t *clcf;

    c = r->connection;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http postpone filter in memory");

    if (r->out == NULL) {
        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

        if (r->headers_out.content_length_n != -1) {
            len = r->headers_out.content_length_n;

            if (len > clcf->subrequest_output_buffer_size) {
                ngx_log_error(NGX_LOG_ERR, c->log, 0,
                              "too big subrequest response: %uz", len);
                return NGX_ERROR;
            }

        } else {
            len = clcf->subrequest_output_buffer_size;
        }

        b = ngx_create_temp_buf(r->pool, len);
        if (b == NULL) {
            return NGX_ERROR;
        }

        b->last_buf = 1;

        r->out = ngx_alloc_chain_link(r->pool);
        if (r->out == NULL) {
            return NGX_ERROR;
        }

        r->out->buf = b;
        r->out->next = NULL;
    }

    b = r->out->buf;

    for ( /* void */ ; in; in = in->next) {

        if (ngx_buf_special(in->buf)) {
            continue;
        }

        len = in->buf->last - in->buf->pos;

        if (len > (size_t) (b->end - b->last)) {
            ngx_log_error(NGX_LOG_ERR, c->log, 0,
                          "too big subrequest response");
            return NGX_ERROR;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                       "http postpone filter in memory %uz bytes", len);

        b->last = ngx_cpymem(b->last, in->buf->pos, len);
        in->buf->pos = in->buf->last;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_postpone_filter_init(ngx_conf_t *cf) {
    ngx_http_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_postpone_filter;

    return NGX_OK;
}
