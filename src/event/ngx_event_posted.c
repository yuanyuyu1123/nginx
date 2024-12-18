
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>

/*实际上,上述问题的解决离不开Nginx的post事件处理机制.这个post事件是什么意思呢?它表示允许事件延后执行.Nginx设计了两个post队列,一
个是由被触发的监听连接的读事件构成的ngx_posted_accept_events队列,另一个是由普通读／写事件构成的ngx_posted_events队列.这样的post事
件可以让用户完成什么样的功能呢?
   将epoll_wait产生的一批事件,分到这两个队列中,让存放着新连接事件的ngx_posted_accept_events队列优先执行,存放普通事件的ngx_posted_events队
列最后执行,这是解决"惊群"和负载均衡两个问题的关键.如果在处理一个事件的过程中产生了另一个事件,而我们希望这个事件随后执行(不是立刻执行),
就可以把它放到post队列中*/
ngx_queue_t ngx_posted_accept_events; //延后处理的新建连接accept事件
ngx_queue_t ngx_posted_next_events;
ngx_queue_t ngx_posted_events; //普通延后连接建立成功后的读写事件

//从posted队列中却出所有ev并执行各个事件的handler
void
ngx_event_process_posted(ngx_cycle_t *cycle, ngx_queue_t *posted) {
    ngx_queue_t *q;
    ngx_event_t *ev;

    while (!ngx_queue_empty(posted)) {

        q = ngx_queue_head(posted);
        ev = ngx_queue_data(q, ngx_event_t, queue);

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "posted event %p", ev);

        ngx_delete_posted_event(ev);

        ev->handler(ev);
    }
}


void
ngx_event_move_posted_next(ngx_cycle_t *cycle) {
    ngx_queue_t *q;
    ngx_event_t *ev;

    for (q = ngx_queue_head(&ngx_posted_next_events);
         q != ngx_queue_sentinel(&ngx_posted_next_events);
         q = ngx_queue_next(q)) {
        ev = ngx_queue_data(q, ngx_event_t, queue);

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "posted next event %p", ev);

        ev->ready = 1;
        ev->available = -1;
    }

    ngx_queue_add(&ngx_posted_events, &ngx_posted_next_events);
    ngx_queue_init(&ngx_posted_next_events);
}
