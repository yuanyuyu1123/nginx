
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>

/*Epoll在LT和ET模式下的读写方式
在一个非阻塞的socket上调用read/write函数, 返回EAGAIN或者EWOULDBLOCK(注: EAGAIN就是EWOULDBLOCK)
从字面上看, 意思是:EAGAIN: 再试一次,EWOULDBLOCK: 如果这是一个阻塞socket, 操作将被block,perror输出: Resource temporarily unavailable
总结:
这个错误表示资源暂时不够,能read时,读缓冲区没有数据,或者write时,写缓冲区满了.遇到这种情况,如果是阻塞socket,
read/write就要阻塞掉.而如果是非阻塞socket,read/write立即返回-1, 同时errno设置为EAGAIN.
所以,对于阻塞socket,read/write返回-1代表网络出错了.但对于非阻塞socket,read/write返回-1不一定网络真的出错了.
可能是Resource temporarily unavailable.这时你应该再试,直到Resource available.
综上,对于non-blocking的socket,正确的读写操作为:
读:忽略掉errno = EAGAIN的错误,下次继续读
写:忽略掉errno = EAGAIN的错误,下次继续写
对于select和epoll的LT模式,这种读写方式是没有问题的.但对于epoll的ET模式,这种方式还有漏洞.
epoll的两种模式LT和ET
二者的差异在于level-trigger模式下只要某个socket处于readable/writable状态,无论什么时候进行epoll_wait都会返回该socket;
而edge-trigger模式下只有某个socket从unreadable变为readable或从unwritable变为writable时,epoll_wait才会返回该socket.
所以,在epoll的ET模式下,正确的读写方式为:
读:只要可读,就一直读,直到返回0,或者 errno = EAGAIN
写:只要可写,就一直写,直到数据发送完,或者 errno = EAGAIN
正确的读
n = 0;
 while ((nread = read(fd, buf + n, BUFSIZ-1)) > 0) {
     n += nread;
 }
 if (nread == -1 && errno != EAGAIN) {
     perror("read error");
 } 正确的写
int nwrite, data_size = strlen(buf);
 n = data_size;
 while (n > 0) {
     nwrite = write(fd, buf + data_size - n, n);
     if (nwrite < n) {
         if (nwrite == -1 && errno != EAGAIN) {
             perror("write error");
         }
         break;
     }
     n -= nwrite;
 } 正确的accept,accept要考虑2个问题
(1) 阻塞模式 accept 存在的问题
考虑这种情况:TCP连接被客户端夭折,即在服务器调用accept之前,客户端主动发送RST终止连接,导致刚刚建立的连接从就绪队列中移出,
如果套接口被设置成阻塞模式,服务器就会一直阻塞在accept调用上,直到其他某个客户建立一个新的连接为止.但是在此期间,服务器单
纯地阻塞在accept调用上,就绪队列中的其他描述符都得不到处理.
解决办法是把监听套接口设置为非阻塞,当客户在服务器调用accept之前中止某个连接时,accept调用可以立即返回-1,这时源自Berkeley的
实现会在内核中处理该事件,并不会将该事件通知给epool,而其他实现把errno设置为ECONNABORTED或者EPROTO错误,我们应该忽略这两个错误.
(2)ET模式下accept存在的问题
考虑这种情况:多个连接同时到达,服务器的TCP就绪队列瞬间积累多个就绪连接,由于是边缘触发模式,epoll只会通知一次,accept只处理
一个连接,导致TCP就绪队列中剩下的连接都得不到处理.
解决办法是用while循环抱住accept调用,处理完TCP就绪队列中的所有连接后再退出循环.如何知道是否处理完就绪队列中的所有连接呢?accept
返回-1并且errno设置为EAGAIN就表示所有连接都处理完.
综合以上两种情况,服务器应该使用非阻塞地accept,accept在ET模式下的正确使用方式为:
while ((conn_sock = accept(listenfd,(struct sockaddr *) &remote, (size_t *)&addrlen)) > 0) {
     handle_client(conn_sock);
 }
 if (conn_sock == -1) {
     if (errno != EAGAIN && errno != ECONNABORTED && errno != EPROTO && errno != EINTR)
     perror("accept");
 } 一道腾讯后台开发的面试题
使用Linuxepoll模型,水平触发模式;当socket可写时,会不停的触发socket可写的事件,如何处理?
第一种最普遍的方式:
需要向socket写数据的时候才把socket加入epoll,等待可写事件.
接受到可写事件后,调用write或者send发送数据.
当所有数据都写完后,把socket移出epoll.
这种方式的缺点是,即使发送很少的数据,也要把socket加入epoll,写完后在移出epoll,有一定操作代价.
一种改进的方式:
开始不把socket加入epoll,需要向socket写数据的时候,直接调用write或者send发送数据.如果返回EAGAIN,把socket加入epoll,在epoll的
驱动下写数据,全部数据发送完毕后,再移出epoll.
这种方式的优点是:数据不多的时候可以避免epoll的事件处理,提高效率.

Epoll不仅会告诉应用程序有I/0事件到来,还会告诉应用程序相关的信息,这些信息是应用程序填充的,因此根据这些信息应用程序就能直接定
位到事件,而不必遍历整个FD集合.
intres = epoll_wait(epfd, events, 20, 120);
for(int i = 0; i < res;i++)
{
    handleEvent(events[n]);
}
    设想一个场景:有100万用户同时与一个进程保持着TCP连接,而每一时刻只有几十个
或几百个TCP连接是活跃的(接收到TCP包),也就是说,在每一时刻,进程只需要处理这
100万连接中的一小部分连接.那么,如何才能高效地处理这种场景呢?进程是否在每次询
问操作系统收集有事件发生的TCP连接时,把这100万个连接告诉操作系统,然后由操作系
统找出其中有事件发生的几百个连接呢?实际上,在Linux内核2.4版本以前,那时的select
或者poll事件驱动方式就是这样做的.
    这里有个非常明显的问题,即在某一时刻,进程收集有事件的连接时,其实这100万连
接中的大部分都是没有事件发生的.因此,如果每次收集事件时,都把这100万连接的套接
字传给操作系统(这首先就是用户态内存到内核态内存的大量复制),而由操作系统内核寻
找这些连接上有没有未处理的事件,将会是巨大的资源浪费,然而select和poll就是这样做
的,因此它们最多只能处理几千个并发连接.而epoll不这样做,它在Linux内核中申请了
一个简易的文件系统,把原先的一个select或者poll调用分成丁3个部分:调用epoll_create
建立1个epoll对象(在epoll文件系统中给这个句柄分配资源)、调用epoll_ctl向epoll对象
中添加这100万个连接的套接字、调用epoll_wait收集发生事件的连接.这样,只需要在进
程启动时建立1个epoll对象,并在需要的时候向它添加或删除连接就可以了,因此,在实
际收集事件时,epoll_wait的效率就会非常高,因为调用epoll_wait时并没有向它传递这100
万个连接,内核也不需要去遍历全部的连接.
    那么,Linux内核将如何实现以上的想法呢?下面以Linux内核2.6.35版本为例,简
单说明一下epoll是如何高效处理事件的.图9-5展示了epoll的内部主要数据结构是如何
安排的.
    当某一个进程调用epoll_create方法时,Linux内核会创建一个eventpoll结构体,这个
结构体中有两个成员与epoll的使用方式密切相关,如下所示.
struct eventpoll  (
    //娃黑树的根节点,这棵树中存储着所有添加到epoll中的事件,也就是这个epoll监控的事件.／
    struct rb_root rbr;
    //双向链表rdllist倮存着将要通过epoll_wait返回给用户的、满足条件的事件
    struct list__head rdllist;
);
    每一个epoll对象都有一个独立的eventpoll结构体,这个结构体会在内核空间中创造独
立的内存,用于存储使用epoll_ctl方法向epoll对象中添加进来的事件.这些事件都会挂到
rbr红黑树中,这样,重复添加的事件就可以通过红黑树而高效地识别出来(epoll_ctl方法会很快)

Linux内核中的这棵红黑树与Nginx红黑树非常相似,可以参照ngx_rbtree_t容器进行理解.
    所有添加到epoll中的事件都会与设备(如网卡)驱动程序建立回调关系,也就是说,
相应的事件发生时会调用这里的回调方法.这个回调方法在内核中叫做ep_poll_callback,它
会把这样的事件放到上面的rdllist双向链表中.这个内核中的双向链表与ngx_queue_t容器
几乎是完全相同的(Nginx代码与Linux内核代码很相似),我们可以参照着理解.在epoll
中,对于每一个事件都会建立一个epitem结构体,如下所示.
struct epitem{
    //红黑树节点,与ngx_rbtree_node七红黑树节点相似
    struct rb_node rbn;
   //双向链表节点,与ngx_queue_t双向链表节点相似
    struct list_head rdllink;
    //事件句柄等信息
    struct epoll_filefd  fd;
    //指向其所属的eventpoll对象
    struct eventpoll *ep;
    //期待的事件类型
    struct epoll_event event;
    );
    这里包含每一个事件对应着的信息.
    当调用epoll_wait检查是否有发生事件的连接时,只是检查eventpon对象中的rdllist双
向链表是否有郎item元素而已,如果rd11ist链表不为空,则把这里的事件复制到用户态内存
中,同时将事件数量返回给用户.因此,epoll_wait的效率非常高.epoll_ctl在向epoll对象
中添加、修改i删除事件时,从rbr红黑树中查找事件也非常快,也就是说,epoll是非常高
效的,它可以轻易地处理百万级别的并发连接.
如何使用epoll:
    收集在epoll监控的事件中已经发生的事件,如果epoll中没有任何一个事件发生,则最
多等待timeout毫秒后返回.epoll_wait的返回值表示当前发生的事件个数,如果返回0,则
表示本次调用中没有事件发生,如果返回-1,则表示出现错误,需要检查errno错误码判断
错误类型.第1个参数epfd是epoll的描述符.第2个参数events则是分配好的epoll_event
结构体数组,如ou将会把发生的事件复制到events数组中(events不可以是空指针,内核只
负责把数据复制到这个events数组中,不会去帮助我们在用户态中分配内存.内核这种做法
效率很高).第3个参数maxevents表示本次可以返回的最大事件数目,通常maxevents参数
与预分配的events数组的大小是相等的.第4个参数timeout表示在没有检测到事件发生时
最多等待的时间(单位为毫秒),如果timeout为0,则表示epoll_wait在rdllist链表中为空,
立刻返回,不会等待.*/
#if (NGX_TEST_BUILD_EPOLL)
/* epoll declarations */
/*
epoll_event中events的取值意义
┏━━━━━━━━┳━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
┃    events取值  ┃    意义                                                                      ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLIN         ┃                                                                              ┃
┃                ┃  表示对应的连接上有数据可以读出(TCP连接的远端主动关闭连接,也相当于可读事   ┃
┃                ┃件,因为需要处理发送来的FIN包)                                               ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLOUT        ┃  表示对应的连接上可以写入数据发送(主动向上游服务器发起非阻塞的TCP连接,连接 ┃
┃                ┃建立成功的事件相当于可写事件)                                                ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLRDHUP      ┃  表示TCP连接的远端关闭或半关闭连接                                           ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLPRI        ┃  表示对应的连接上有紧急数据需要读                                            ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLERR        ┃  表示对应的连接发生错误                                                      ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLHUP        ┃  表示对应的连接被挂起                                                        ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLET         ┃  表示将触发方式设置妁边缘触发(ET),系统默认为水平触发(LT’)                  ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLONESHOT    ┃  表示对这个事件只处理一次,下次需要处理时需重新加入epoll                     ┃
┗━━━━━━━━┻━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
简述Linux Epoll ET模式EPOLLOUT和EPOLLIN触发时刻 ET模式称为边缘触发模式,顾名思义,不到边缘情况,是死都不会触发的.
EPOLLOUT事件:
EPOLLOUT事件只有在连接时触发一次,表示可写,其他时候想要触发,那你要先准备好下面条件:
1.某次write,写满了发送缓冲区,返回错误码为EAGAIN.
2.对端读取了一些数据,又重新可写了,此时会触发EPOLLOUT.
简单地说:EPOLLOUT事件只有在不可写到可写的转变时刻,才会触发一次,所以叫边缘触发,这叫法没错的！
其实,如果你真的想强制触发一次,也是有办法的,直接调用epoll_ctl重新设置一下event就可以了,event跟原来的设置一模一样都行(但必须包含EPOLLOUT),关键是重新设置,就会马上触发一次EPOLLOUT事件.
EPOLLIN事件:
EPOLLIN事件则只有当对端有数据写入时才会触发,所以触发一次后需要不断读取所有数据直到读完EAGAIN为止.否则剩下的数据只有在下次对端有写入时才能一起取出来了.
*/
/* epoll declarations */

#define EPOLLIN        0x001
#define EPOLLPRI       0x002
#define EPOLLOUT       0x004
#define EPOLLERR       0x008 //EPOLLERR|EPOLLHUP都表示连接异常情况  fd用完或者其他吧  //epoll EPOLLERR|EPOLLHUP实际上是通过触发读写事件进行读写操作recv write来检测连接异常
#define EPOLLHUP       0x010
#define EPOLLRDNORM    0x040
#define EPOLLRDBAND    0x080
#define EPOLLWRNORM    0x100
#define EPOLLWRBAND    0x200 //当对端已经关闭,本端写数据,会引起该事件
#define EPOLLMSG       0x400

#define EPOLLRDHUP     0x2000

#define EPOLLEXCLUSIVE 0x10000000
//设置该标记后读取完数据后,如果正在处理数据过程中又有新的数据到来,不会触发epoll_wait返回,除非数据处理完毕后重新add epoll_ctl操作,参考<linux高性能服务器开发>9.3.4节
#define EPOLLONESHOT   0x40000000
#define EPOLLET        0x80000000 //表示将触发方式设置妁边缘触发(ET),系统默认为水平触发(LT’)

#define EPOLL_CTL_ADD  1
#define EPOLL_CTL_DEL  2
#define EPOLL_CTL_MOD  3

typedef union epoll_data {
    void         *ptr; //在ngx_epoll_add_event中赋值为ngx_connection_t类型
    int           fd;
    uint32_t      u32;
    uint64_t      u64;
} epoll_data_t;

struct epoll_event {
    /*
表9-3 epoll_event中events的取值意义
┏━━━━━━━━┳━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
┃    events取值  ┃    意义                                                                      ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLIN         ┃                                                                              ┃
┃                ┃  表示对应的连接上有数据可以读出(TCP连接的远端主动关闭连接,也相当于可读事   ┃
┃                ┃件,因为需要处理发送来的FIN包)                                               ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLOUT        ┃  表示对应的连接上可以写入数据发送(主动向上游服务器发起非阻塞的TCP连接,连接 ┃
┃                ┃建立成功的事件相当于可写事件)                                                ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLRDHUP      ┃  表示TCP连接的远端关闭或半关闭连接                                           ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLPRI        ┃  表示对应的连接上有紧急数据需要读                                            ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLERR        ┃  表示对应的连接发生错误                                                      ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLHUP        ┃  表示对应的连接被挂起                                                        ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLET         ┃  表示将触发方式设置妁边缘触发(ET),系统默认为水平触发(L1’)                  ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLONESHOT    ┃  表示对这个事件只处理一次,下次需要处理时需重新加入epoll                     ┃
┗━━━━━━━━┻━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
*/
    uint32_t      events;
    epoll_data_t  data;
};


int epoll_create(int size);

int epoll_create(int size)
{
    return -1;
}


int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
//EPOLL_CTL_ADD一次后,就可以一直通过epoll_wait来获取读事件,除非调用EPOLL_CTL_DEL,不是每次读事件触发epoll_wait返回后都要重新添加EPOLL_CTL_ADD,
//之前代码中有的地方好像备注错了,备注为每次读事件触发后都要重新add一次,epoll_ctr add写事件一样
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    return -1;
}


int epoll_wait(int epfd, struct epoll_event *events, int nevents, int timeout);
/*
//ngx_notify->ngx_epoll_notify只会触发epoll_in,不会同时引发epoll_out,如果是网络读事件epoll_in,则会同时引起epoll_out
*/
int epoll_wait(int epfd, struct epoll_event *events, int nevents, int timeout)
{
    return -1;
}

#if (NGX_HAVE_EVENTFD)
#define SYS_eventfd       323
#endif

#if (NGX_HAVE_FILE_AIO)

#define SYS_io_setup      245
#define SYS_io_destroy    246
#define SYS_io_getevents  247

typedef u_int  aio_context_t;

struct io_event {
    uint64_t  data;  /* the data field from the iocb */ //与提交事件时对应的iocb结构体中的aio_data是一致的
    uint64_t  obj;   /* what iocb this event came from */  //指向提交事件时对应的iocb结构体
    int64_t   res;   /* result code for this event */
    int64_t   res2;  /* secondary result */
};


#endif
#endif /* NGX_TEST_BUILD_EPOLL */

//成员信息见ngx_epoll_commands
typedef struct {
    /*events是调用epoll_wait方法时传人的第3个参数maxevents,而第2个参数events数组的大小也是由它决定的,下面将在ngx_epoll_init方法中初始化这个数组*/
    ngx_uint_t events; // "epoll_events"参数设置  默认512 见ngx_epoll_init_conf
    ngx_uint_t aio_requests; // "worker_aio_requests"参数设置  默认32 见ngx_epoll_init_conf
} ngx_epoll_conf_t;


static ngx_int_t ngx_epoll_init(ngx_cycle_t *cycle, ngx_msec_t timer);

#if (NGX_HAVE_EVENTFD)

static ngx_int_t ngx_epoll_notify_init(ngx_log_t *log);

static void ngx_epoll_notify_handler(ngx_event_t *ev);

#endif
#if (NGX_HAVE_EPOLLRDHUP)

static void ngx_epoll_test_rdhup(ngx_cycle_t *cycle);

#endif

static void ngx_epoll_done(ngx_cycle_t *cycle);

static ngx_int_t ngx_epoll_add_event(ngx_event_t *ev, ngx_int_t event,
                                     ngx_uint_t flags);

static ngx_int_t ngx_epoll_del_event(ngx_event_t *ev, ngx_int_t event,
                                     ngx_uint_t flags);

static ngx_int_t ngx_epoll_add_connection(ngx_connection_t *c);

static ngx_int_t ngx_epoll_del_connection(ngx_connection_t *c,
                                          ngx_uint_t flags);

#if (NGX_HAVE_EVENTFD)

static ngx_int_t ngx_epoll_notify(ngx_event_handler_pt handler);

#endif

static ngx_int_t ngx_epoll_process_events(ngx_cycle_t *cycle, ngx_msec_t timer,
                                          ngx_uint_t flags);

#if (NGX_HAVE_FILE_AIO)
static void ngx_epoll_eventfd_handler(ngx_event_t *ev);
#endif

static void *ngx_epoll_create_conf(ngx_cycle_t *cycle);

static char *ngx_epoll_init_conf(ngx_cycle_t *cycle, void *conf);

static int ep = -1; //ngx_epoll_init -> epoll_create的返回值
static struct epoll_event *event_list; //epoll_events个sizeof(struct epoll_event) * nevents, 见ngx_epoll_init
static ngx_uint_t nevents; //nerents也是配置项epoll_events的参数

#if (NGX_HAVE_EVENTFD)
/*执行ngx_epoll_notify后会通过epoll_wait返回执行该函数ngx_epoll_notify_handler
ngx_epoll_notify_handler  ngx_epoll_notify_init  ngx_epoll_notify(ngx_notify)配合阅读*/
static int notify_fd = -1; //初始化见ngx_epoll_notify_init
static ngx_event_t notify_event;
static ngx_connection_t notify_conn;
#endif

#if (NGX_HAVE_FILE_AIO)
//用于通知异步I/O事件的描述符,它与iocb结构体中的aio_resfd成员是一致的,通过该fd添加到epoll事件中,从而可以检测异步io事件
int                         ngx_eventfd = -1;
//异步I/O的上下文,全局唯一,必须经过io_setup初始化才能使用
aio_context_t               ngx_aio_ctx = 0; //ngx_epoll_aio_init->io_setup创建
//异步I/O事件完成后进行通知的描述符,也就是ngx_eventfd所对应的ngx_event_t事件
static ngx_event_t          ngx_eventfd_event;  //读事件
//异步I/O事件完成后进行通知的描述符ngx_eventfd所对应的ngx_connectiont连接
static ngx_connection_t     ngx_eventfd_conn;

#endif

#if (NGX_HAVE_EPOLLRDHUP)
ngx_uint_t ngx_use_epoll_rdhup;
#endif

static ngx_str_t epoll_name = ngx_string("epoll");

static ngx_command_t ngx_epoll_commands[] = {
        /*在调用epoll_wait时,将由第2和第3个参数告诉Linux内核一次最多可返回多少个事件.这个配置项表示调用一次epoll_wait时最多可返回
        的事件数,当然,它也会预分配那么多epoll_event结构体用于存储事件*/

        {ngx_string("epoll_events"),
         NGX_EVENT_CONF | NGX_CONF_TAKE1,
         ngx_conf_set_num_slot,
         0,
         offsetof(ngx_epoll_conf_t, events),
         NULL},
        /*在开启异步I/O且使用io_setup系统调用初始化异步I/O上下文环境时,初始分配的异步I/O事件个数*/
        {ngx_string("worker_aio_requests"),
         NGX_EVENT_CONF | NGX_CONF_TAKE1,
         ngx_conf_set_num_slot,
         0,
         offsetof(ngx_epoll_conf_t, aio_requests),
         NULL},

        ngx_null_command
};


static ngx_event_module_t ngx_epoll_module_ctx = {
        &epoll_name,
        ngx_epoll_create_conf,               /* create configuration */
        ngx_epoll_init_conf,                 /* init configuration */

        {
                ngx_epoll_add_event,             /* add an event */
                ngx_epoll_del_event,             /* delete an event */
                ngx_epoll_add_event,             /* enable an event */
                ngx_epoll_del_event,             /* disable an event */
                ngx_epoll_add_connection,        /* add an connection */
                ngx_epoll_del_connection,        /* delete an connection */
#if (NGX_HAVE_EVENTFD)
                ngx_epoll_notify,                /* trigger a notify */
#else
                NULL,                            /* trigger a notify */
#endif
                ngx_epoll_process_events,        /* process the events */
                ngx_epoll_init,                  /* init the events */  //在创建的子进程中执行
                ngx_epoll_done,                  /* done the events */
        }
};

ngx_module_t ngx_epoll_module = {
        NGX_MODULE_V1,
        &ngx_epoll_module_ctx,               /* module context */
        ngx_epoll_commands,                  /* module directives */
        NGX_EVENT_MODULE,                    /* module type */
        NULL,                                /* init master */
        NULL,                                /* init module */
        NULL,                                /* init process */
        NULL,                                /* init thread */
        NULL,                                /* exit thread */
        NULL,                                /* exit process */
        NULL,                                /* exit master */
        NGX_MODULE_V1_PADDING
};


#if (NGX_HAVE_FILE_AIO)

/*
 * We call io_setup(), io_destroy() io_submit(), and io_getevents() directly
 * as syscalls instead of libaio usage, because the library header file
 * supports eventfd() since 0.3.107 version only.
 */

//初始化文件异步I/O的上下文
static int
io_setup(u_int nr_reqs, aio_context_t *ctx) //相当于epoll中的epoll_create
{
    return syscall(SYS_io_setup, nr_reqs, ctx);
}


static int
io_destroy(aio_context_t ctx)
{
    return syscall(SYS_io_destroy, ctx);
}

// 如何获取已经完成的异步I/O事件呢?io_getevents方法可以做到,它相当于epoll中的epoll_wait方法
static int
io_getevents(aio_context_t ctx, long min_nr, long nr, struct io_event *events,
    struct timespec *tmo) //io_submit为添加事件
{
    return syscall(SYS_io_getevents, ctx, min_nr, nr, events, tmo);
}

/*ngx_epoll_aio_init初始化aio事件列表, ngx_file_aio_read添加读文件事件,当读取完毕后epoll会触发
ngx_epoll_eventfd_handler->ngx_file_aio_event_handler
nginx file aio只提供了read接口,不提供write接口,因为异步aio只从磁盘读和写,而非aio方式一般写会落到
磁盘缓存,所以不提供该接口,如果异步io写可能会更慢*/

//aio使用可以参考ngx_http_file_cache_aio_read  ngx_output_chain_copy_buf 都是为读取文件准备的
static void
ngx_epoll_aio_init(ngx_cycle_t *cycle, ngx_epoll_conf_t *epcf)
{
    int                 n;
    struct epoll_event  ee;
/*调用eventfd系统调用新建一个eventfd对象,第二个参数中的0表示eventfd的计数器初始值为0,系统调用成功返回的是与eventfd对象关联的描述符*/
#if (NGX_HAVE_SYS_EVENTFD_H)
    ngx_eventfd = eventfd(0, 0);
#else
    ngx_eventfd = syscall(SYS_eventfd, 0); //使用Linux中第323个系统调用获取一个描述符句柄
#endif

    if (ngx_eventfd == -1) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "eventfd() failed");
        ngx_file_aio = 0;
        return;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "eventfd: %d", ngx_eventfd);

    n = 1;

    if (ioctl(ngx_eventfd, FIONBIO, &n) == -1) { //设置ngx_eventfd为无阻塞
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "ioctl(eventfd, FIONBIO) failed");
        goto failed;
    }

    if (io_setup(epcf->aio_requests, &ngx_aio_ctx) == -1) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "io_setup() failed");
        goto failed;
    }

    ngx_eventfd_event.data = &ngx_eventfd_conn; //设置用于异步I/O完成通知的ngx_eventfd_event事件,它与ngx_connection_t连接是对应的
    //该函数在ngx_epoll_process_events中执行
    ngx_eventfd_event.handler = ngx_epoll_eventfd_handler; //在异步I/O事件完成后,使用ngx_epoll_eventfd handler方法处理
    ngx_eventfd_event.log = cycle->log;
    ngx_eventfd_event.active = 1;
    ngx_eventfd_conn.fd = ngx_eventfd;
    ngx_eventfd_conn.read = &ngx_eventfd_event; //ngx_eventfd_conn连接的读事件就是上面的ngx_eventfd_event
    ngx_eventfd_conn.log = cycle->log;

    ee.events = EPOLLIN|EPOLLET;
    ee.data.ptr = &ngx_eventfd_conn;
    //向epoll中添加到异步I/O的通知描述符ngx_eventfd
    /*这样,ngx_epoll_aio init方法会把异步I/O与epoll结合起来,当某一个异步I/O事件完成后,ngx_eventfd旬柄就处于可用状态,这样epoll_wait在
    返回ngx_eventfd event事件后就会调用它的回调方法ngx_epoll_eventfd handler处理已经完成的异步I/O事件*/
    if (epoll_ctl(ep, EPOLL_CTL_ADD, ngx_eventfd, &ee) != -1) { //成功后直接返回
        return;
    }

    ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                  "epoll_ctl(EPOLL_CTL_ADD, eventfd) failed");
     //epoll_ctl失败时需毁销aio的context
    if (io_destroy(ngx_aio_ctx) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "io_destroy() failed");
    }

failed:

    if (close(ngx_eventfd) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "eventfd close() failed");
    }

    ngx_eventfd = -1;
    ngx_aio_ctx = 0;
    ngx_file_aio = 0;
}

#endif


static ngx_int_t
ngx_epoll_init(ngx_cycle_t *cycle, ngx_msec_t timer) {
    ngx_epoll_conf_t *epcf;

    epcf = ngx_event_get_conf(cycle->conf_ctx, ngx_epoll_module);

    if (ep == -1) {
        /*epoll_create返回一个句柄,之后epoll的使用都将依靠这个句柄来标识.参数size是告诉epoll所要处理的大致事件数目.不再使用epoll时,
       必须调用close关闭这个句柄.注意size参数只是告诉内核这个epoll对象会处理的事件大致数目,而不是能够处理的事件的最大个数.在Linux蕞
       新的一些内核版本的实现中,这个size参数没有任何意义.
       调用epoll_create在内核中创建epoll对象.上文已经讲过,参数size不是用于指明epoll能够处理的最大事件个数,因为在许多Linux内核
       版本中,epoll是不处理这个参数的,所以设为cycle->connectionn/2(而不是cycle->connection_n)也不要紧*/
        ep = epoll_create(cycle->connection_n / 2);

        if (ep == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "epoll_create() failed");
            return NGX_ERROR;
        }

#if (NGX_HAVE_EVENTFD)
        if (ngx_epoll_notify_init(cycle->log) != NGX_OK) {
            ngx_epoll_module_ctx.actions.notify = NULL;
        }
#endif

#if (NGX_HAVE_FILE_AIO)
        ngx_epoll_aio_init(cycle, epcf);
#endif

#if (NGX_HAVE_EPOLLRDHUP)
        ngx_epoll_test_rdhup(cycle);
#endif
    }

    if (nevents < epcf->events) {
        if (event_list) {
            ngx_free(event_list);
        }

        event_list = ngx_alloc(sizeof(struct epoll_event) * epcf->events,
                               cycle->log);
        if (event_list == NULL) {
            return NGX_ERROR;
        }
    }

    nevents = epcf->events; //nerents也是配置项epoll_events的参数

    ngx_io = ngx_os_io;

    ngx_event_actions = ngx_epoll_module_ctx.actions;

#if (NGX_HAVE_CLEAR_EVENT) //默认是采用LT模式来使用epoll的,NGX_USE_CLEAR_EVENT宏实际上就是在告诉Nginx使用ET模式
    ngx_event_flags = NGX_USE_CLEAR_EVENT
                      #else
                      ngx_event_flags = NGX_USE_LEVEL_EVENT
                      #endif
                      | NGX_USE_GREEDY_EVENT
                      | NGX_USE_EPOLL_EVENT;

    return NGX_OK;
}


#if (NGX_HAVE_EVENTFD)
/*执行ngx_epoll_notify后会通过epoll_wait返回执行该函数ngx_epoll_notify_handler
ngx_epoll_notify_handler  ngx_epoll_notify_init  ngx_epoll_notify(ngx_notify)配合阅读*/
static ngx_int_t
ngx_epoll_notify_init(ngx_log_t *log) {
    struct epoll_event ee;

#if (NGX_HAVE_SYS_EVENTFD_H)
    notify_fd = eventfd(0, 0);
#else
    notify_fd = syscall(SYS_eventfd, 0);
#endif

    if (notify_fd == -1) {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno, "eventfd() failed");
        return NGX_ERROR;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, log, 0,
                   "notify eventfd: %d", notify_fd);

    notify_event.handler = ngx_epoll_notify_handler;
    notify_event.log = log;
    notify_event.active = 1;

    notify_conn.fd = notify_fd;
    notify_conn.read = &notify_event;
    notify_conn.log = log;

    ee.events = EPOLLIN | EPOLLET;
    ee.data.ptr = &notify_conn;

    if (epoll_ctl(ep, EPOLL_CTL_ADD, notify_fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                      "epoll_ctl(EPOLL_CTL_ADD, eventfd) failed");

        if (close(notify_fd) == -1) {
            ngx_log_error(NGX_LOG_ALERT, log, ngx_errno,
                          "eventfd close() failed");
        }

        return NGX_ERROR;
    }

    return NGX_OK;
}

/*执行ngx_epoll_notify后会通过epoll_wait返回执行该函数ngx_epoll_notify_handler
ngx_epoll_notify_handler  ngx_epoll_notify_init  ngx_epoll_notify(ngx_notify)配合阅读*/

static void //ngx_epoll_notify_handler  ngx_epoll_notify_init  ngx_epoll_notify(ngx_notify)配合阅读
ngx_epoll_notify_handler(ngx_event_t *ev) {
    ssize_t n;
    uint64_t count;
    ngx_err_t err;
    ngx_event_handler_pt handler;

    if (++ev->index == NGX_MAX_UINT32_VALUE) {
        ev->index = 0;

        n = read(notify_fd, &count, sizeof(uint64_t));

        err = ngx_errno;

        ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                       "read() eventfd %d: %z count:%uL", notify_fd, n, count);

        if ((size_t) n != sizeof(uint64_t)) {
            ngx_log_error(NGX_LOG_ALERT, ev->log, err,
                          "read() eventfd %d failed", notify_fd);
        }
    }

    handler = ev->data;
    handler(ev);
}

#endif


#if (NGX_HAVE_EPOLLRDHUP)

static void
ngx_epoll_test_rdhup(ngx_cycle_t *cycle) {
    int s[2], events;
    struct epoll_event ee;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, s) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "socketpair() failed");
        return;
    }

    ee.events = EPOLLET | EPOLLIN | EPOLLRDHUP;

    if (epoll_ctl(ep, EPOLL_CTL_ADD, s[0], &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "epoll_ctl() failed");
        goto failed;
    }

    if (close(s[1]) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "close() failed");
        s[1] = -1;
        goto failed;
    }

    s[1] = -1;

    events = epoll_wait(ep, &ee, 1, 5000);

    if (events == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "epoll_wait() failed");
        goto failed;
    }

    if (events) {
        ngx_use_epoll_rdhup = ee.events & EPOLLRDHUP;

    } else {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, NGX_ETIMEDOUT,
                      "epoll_wait() timed out");
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "testing the EPOLLRDHUP flag: %s",
                   ngx_use_epoll_rdhup ? "success" : "fail");

    failed:

    if (s[1] != -1 && close(s[1]) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "close() failed");
    }

    if (close(s[0]) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "close() failed");
    }
}

#endif

/*
ngx_event_actions_t done接口是由ngx_epoll_done方法实现的,在Nginx退出服务时它会得到调用.ngx_epoll_done主要是关闭epoll描述符ep,
同时释放event_1ist数组*/
static void
ngx_epoll_done(ngx_cycle_t *cycle) {
    if (close(ep) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "epoll close() failed");
    }

    ep = -1;

#if (NGX_HAVE_EVENTFD)

    if (close(notify_fd) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "eventfd close() failed");
    }

    notify_fd = -1;

#endif

#if (NGX_HAVE_FILE_AIO)

    if (ngx_eventfd != -1) {

        if (io_destroy(ngx_aio_ctx) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "io_destroy() failed");
        }

        if (close(ngx_eventfd) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "eventfd close() failed");
        }

        ngx_eventfd = -1;
    }

    ngx_aio_ctx = 0;

#endif

    ngx_free(event_list);

    event_list = NULL;
    nevents = 0;
}

/*epoll_ctl系统调用
    epoll_ctl在C库中的原型如下.
    int epoll_ctl(int  epfd, int  op, int  fd, struct  epoll_event*  event)j
    epoll_ctl向epoll对象中添加、修改或者删除感兴趣的事件,返回0表示成功,否则返
回一1,此时需要根据errno错误码判断错误类型.epoll_wait方法返回的事件必然是通过
epoll_ctl添加刮epoll中的.参数epfd是epoll_create返回的句柄,而op参数的意义见表
9-2.
┏━━━━━━━━━━┳━━━━━━━━━━━━━┓
┃    ‘    op的取值  ┃    意义                  ┃
┣━━━━━━━━━━╋━━━━━━━━━━━━━┫
┃I EPOLL_CTL_ADD     ┃    添加新的事件到epoll中 ┃
┣━━━━━━━━━━╋━━━━━━━━━━━━━┫
┃I EPOLL_CTL MOD     ┃    修改epoll中的事件     ┃
┣━━━━━━━━━━╋━━━━━━━━━━━━━┫
┃I EPOLL_CTL_DEL     ┃    删除epoll中的事件     ┃
┗━━━━━━━━━━┻━━━━━━━━━━━━━┛
    第3个参数fd是待监测的连接套接字,第4个参数是在告诉epoll对什么样的事件感
兴趣,它使用了epoll_event结构体,在上文介绍过的epoll实现机制中会为每一个事件创
建epitem结构体,而在epitem中有一个epoll_event类型的event成员.下面看一下epoll_
event的定义.
struct epoll_event{
        _uint32 t events;
        epoll data_t data;
};
events的取值见表9-3.
表9-3 epoll_event中events的取值意义
┏━━━━━━━━┳━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
┃    events取值  ┃    意义                                                                      ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLIN         ┃                                                                              ┃
┃                ┃  表示对应的连接上有数据可以读出(TCP连接的远端主动关闭连接,也相当于可读事   ┃
┃                ┃件,因为需要处理发送来的FIN包)                                               ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLOUT        ┃  表示对应的连接上可以写入数据发送(主动向上游服务器发起非阻塞的TCP连接,连接 ┃
┃                ┃建立成功的事件相当于可写事件)                                                ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLRDHUP      ┃  表示TCP连接的远端关闭或半关闭连接                                           ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLPRI        ┃  表示对应的连接上有紧急数据需要读                                            ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLERR        ┃  表示对应的连接发生错误                                                      ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLHUP        ┃  表示对应的连接被挂起                                                        ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLET         ┃  表示将触发方式设置妁边缘触发(ET),系统默认为水平触发(LT’)                  ┃
┣━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃EPOLLONESHOT    ┃  表示对这个事件只处理一次,下次需要处理时需重新加入epoll                     ┃
┗━━━━━━━━┻━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
而data成员是一个epoll_data联合,其定义如下.
typedef union epoll_data
       void        *ptr;
      int           fd;
     uint32_t            u32 ;
     uint64_t            u64 ;
} epoll_data_t;
    可见,这个data成员还与具体的使用方式相关.例如,ngx_epoll_module模块只使用了
联合中的ptr成员,作为指向ngx_connection_t连接的指针*/

/*ngx_epoll_add_event表示添加某种类型的(读或者写,通过flag指定促发方式,NGX_CLEAR_EVENT为ET方式,NGX_LEVEL_EVENT为LT方式)事件,
ngx_epoll_add_connection(读写一起添加上去, 使用EPOLLET边沿触发方式)*/
static ngx_int_t //通过flag指定促发方式,NGX_CLEAR_EVENT为ET方式,NGX_LEVEL_EVENT为LT方式
ngx_epoll_add_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags) { //该函数封装为ngx_add_event的,使用的时候为ngx_add_event
    //一般网络事件中的报文读写通过ngx_handle_read_event  ngx_handle_write_event添加事件
    int op;
    uint32_t events, prev;
    ngx_event_t *e;
    ngx_connection_t *c;
    struct epoll_event ee;

    c = ev->data; //每个事件的data成员都存放着其对应的ngx_connection_t连接
    /*下面会根据event参数确定当前事件是读事件还是写事件,这会决定eventg是加上EPOLLIN标志位还是EPOLLOUT标志位*/
    events = (uint32_t) event;

    if (event == NGX_READ_EVENT) {
        e = c->write;
        prev = EPOLLOUT;
#if (NGX_READ_EVENT != EPOLLIN | EPOLLRDHUP)
        events = EPOLLIN|EPOLLRDHUP;
#endif

    } else {
        e = c->read;
        prev = EPOLLIN | EPOLLRDHUP;
#if (NGX_WRITE_EVENT != EPOLLOUT)
        events = EPOLLOUT;
#endif
    }
    //第一次添加epoll_ctl为EPOLL_CTL_ADD,如果再次添加发现active为1,则epoll_ctl为EPOLL_CTL_MOD
    if (e->active) { //根据active标志位确定是否为活跃事件,以决定到底是修改还是添加事件
        op = EPOLL_CTL_MOD;
        events |= prev; //如果是active的,则events= EPOLLIN|EPOLLRDHUP|EPOLLOUT;

    } else {
        op = EPOLL_CTL_ADD;
    }

#if (NGX_HAVE_EPOLLEXCLUSIVE && NGX_HAVE_EPOLLRDHUP)
    if (flags & NGX_EXCLUSIVE_EVENT) {
        events &= ~EPOLLRDHUP;
    }
#endif

    ee.events = events | (uint32_t) flags; //加入flags参数到events标志位中
    /* ptr成员存储的是ngx_connection_t连接,可参见epoll的使用方式.*/
    ee.data.ptr = (void *) ((uintptr_t) c | ev->instance);

    ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                   "epoll add event: fd:%d op:%d ev:%08XD",
                   c->fd, op, ee.events);
    /*EPOLL_CTL_ADD一次后,就可以一直通过epoll_wait来获取读事件,除非调用EPOLL_CTL_DEL,不是每次读事件触发epoll_wait返回后都要重新添加EPOLL_CTL_ADD,
    之前代码中有的地方好像备注错了,备注为每次读事件触发后都要重新add一次*/
    if (epoll_ctl(ep, op, c->fd, &ee) == -1) { //epoll_wait() 系统调用等待由文件描述符 c->fd 引用的 epoll 实例上的事件
        ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
                      "epoll_ctl(%d, %d) failed", op, c->fd);
        return NGX_ERROR;
    }
    /*后面的ngx_add_event->ngx_epoll_add_event中把listening中的c->read->active置1, ngx_epoll_del_event中把listening中置read->active置0
    第一次添加epoll_ctl为EPOLL_CTL_ADD,如果再次添加发现active为1,则epoll_ctl为EPOLL_CTL_MOD*/
    ev->active = 1; //将事件的active标志位置为1,表示当前事件是活跃的   ngx_epoll_del_event中置0
#if 0
    ev->oneshot = (flags & NGX_ONESHOT_EVENT) ? 1 : 0;
#endif

    return NGX_OK;
}


static ngx_int_t
ngx_epoll_del_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags) {
    int op;
    uint32_t prev;
    ngx_event_t *e;
    ngx_connection_t *c;
    struct epoll_event ee;

    /*
     * when the file descriptor is closed, the epoll automatically deletes
     * it from its queue, so we do not need to delete explicitly the event
     * before the closing the file descriptor
     */

    if (flags & NGX_CLOSE_EVENT) {
        ev->active = 0;
        return NGX_OK;
    }

    c = ev->data;

    if (event == NGX_READ_EVENT) {
        e = c->write;
        prev = EPOLLOUT;

    } else {
        e = c->read;
        prev = EPOLLIN | EPOLLRDHUP;
    }

    if (e->active) {
        op = EPOLL_CTL_MOD;
        ee.events = prev | (uint32_t) flags;
        ee.data.ptr = (void *) ((uintptr_t) c | ev->instance);

    } else {
        op = EPOLL_CTL_DEL;
        ee.events = 0;
        ee.data.ptr = NULL;
    }

    ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                   "epoll del event: fd:%d op:%d ev:%08XD",
                   c->fd, op, ee.events);

    if (epoll_ctl(ep, op, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
                      "epoll_ctl(%d, %d) failed", op, c->fd);
        return NGX_ERROR;
    }
    //后面的ngx_add_event->ngx_epoll_add_event中把listening中的c->read->active置1, ngx_epoll_del_event中把listening中置read->active置0
    ev->active = 0;

    return NGX_OK;
}

//后面的ngx_add_event->ngx_epoll_add_event中把listening中的c->read->active置1, ngx_epoll_del_event中把listening中置read->active置0
static ngx_int_t
ngx_epoll_add_connection(ngx_connection_t *c) { //该函数封装为ngx_add_conn的,使用的时候为ngx_add_conn
    struct epoll_event ee;

    ee.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP; //注意这里是水平触发
    ee.data.ptr = (void *) ((uintptr_t) c | c->read->instance);

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, c->log, 0,
                   "epoll add connection: fd:%d ev:%08XD", c->fd, ee.events);

    if (epoll_ctl(ep, EPOLL_CTL_ADD, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      "epoll_ctl(EPOLL_CTL_ADD, %d) failed", c->fd);
        return NGX_ERROR;
    }

    c->read->active = 1;
    c->write->active = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_epoll_del_connection(ngx_connection_t *c, ngx_uint_t flags) {
    int op;
    struct epoll_event ee;

    /*
     * when the file descriptor is closed the epoll automatically deletes
     * it from its queue so we do not need to delete explicitly the event
     * before the closing the file descriptor
     */

    if (flags & NGX_CLOSE_EVENT) { //如果该函数紧接着会调用close(fd),那么就不用epoll del了,系统会自动del
        c->read->active = 0;
        c->write->active = 0;
        return NGX_OK;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, c->log, 0,
                   "epoll del connection: fd:%d", c->fd);

    op = EPOLL_CTL_DEL;
    ee.events = 0;
    ee.data.ptr = NULL;

    if (epoll_ctl(ep, op, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      "epoll_ctl(%d, %d) failed", op, c->fd);
        return NGX_ERROR;
    }

    c->read->active = 0;
    c->write->active = 0;

    return NGX_OK;
}


#if (NGX_HAVE_EVENTFD)
/*执行ngx_epoll_notify后会通过epoll_wait返回执行该函数ngx_epoll_notify_handler
ngx_epoll_notify_handler  ngx_epoll_notify_init  ngx_epoll_notify(ngx_notify)配合阅读*/
static ngx_int_t
ngx_epoll_notify(ngx_event_handler_pt handler) {
    static uint64_t inc = 1;

    notify_event.data = handler;
    //ngx_notify->ngx_epoll_notify只会触发epool_in,不会同时引发epoll_out,如果是网络读事件epoll_in,则会同时引起epoll_out
    if ((size_t) write(notify_fd, &inc, sizeof(uint64_t)) != sizeof(uint64_t)) {
        ngx_log_error(NGX_LOG_ALERT, notify_event.log, ngx_errno,
                      "write() to eventfd %d failed", notify_fd);
        return NGX_ERROR;
    }

    return NGX_OK;
}

#endif

/*ngx_epoll_process_events注册到ngx_process_events的和ngx_epoll_add_event配合使用,
该函数在ngx_process_events_and_timers中调用*/
static ngx_int_t
ngx_epoll_process_events(ngx_cycle_t *cycle, ngx_msec_t timer, ngx_uint_t flags) { //flags参数中含有NGX_POST_EVENTS表示这批事件要延后处理
    int events;
    uint32_t revents;
    ngx_int_t instance, i;
    ngx_uint_t level;
    ngx_err_t err;
    ngx_event_t *rev, *wev;
    ngx_queue_t *queue;
    ngx_connection_t *c;

    /* NGX_TIMER_INFINITE == INFTIM */

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "epoll timer: %M", timer);
    /*调用epoll_wait获取事件.注意,timer参数是在process_events调用时传入的,在9.7和9.8节中会提到这个参数*/

    /*The call was interrupted by a signal handler before any of the requested events occurred or the timeout expired;
    如果有信号发生(见函数ngx_timer_signal_handler),如定时器,则会返回-1
    需要和ngx_add_event与ngx_add_conn配合使用
    event_list存储的是就绪好的事件,如果是select则是传入用户注册的事件,需要遍历检查,而且每次select返回后需要重新设置事件集,epoll不用*/

    /*这里面等待的事件包括客户端连接事件(这个是从父进程继承过来的ep,然后在子进程while前的ngx_event_process_init->ngx_add_event添加),
    对已经建立连接的fd读写事件的添加在ngx_event_accept->ngx_http_init_connection->ngx_handle_read_event*/

    /*ngx_notify->ngx_epoll_notify只会触发epoll_in,不会同时引发epoll_out,如果是网络读事件epoll_in,则会同时引起epoll_out*/
    events = epoll_wait(ep, event_list, (int) nevents, timer); //timer为-1表示无限等待, nevents表示最多监听多少个事件,必须大于0
    //EPOLL_WAIT如果没有读写事件或者定时器超时事件发生,则会进入睡眠,这个过程会让出CPU
    err = (events == -1) ? ngx_errno : 0;
    /*当flags标志位指示要更新时间时,就是在这里更新的,要么ngx_timer_resolution毫秒超时后跟新时间,要摸epoll读写事件超时后跟新时间*/
    if (flags & NGX_UPDATE_TIME || ngx_event_timer_alarm) {
        ngx_time_update();
    }

    if (err) {
        if (err == NGX_EINTR) {

            if (ngx_event_timer_alarm) { //定时器超时引起的epoll_wait返回
                ngx_event_timer_alarm = 0;
                return NGX_OK;
            }

            level = NGX_LOG_INFO;

        } else {
            level = NGX_LOG_ALERT;
        }

        ngx_log_error(level, cycle->log, err, "epoll_wait() failed");
        return NGX_ERROR;
    }

    if (events == 0) {
        if (timer != NGX_TIMER_INFINITE) {
            return NGX_OK;
        }

        ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                      "epoll_wait() returned no events without timeout");
        return NGX_ERROR;
    }
    //遍历本次epoll_wait返回的所有事件
    for (i = 0; i < events; i++) { //和ngx_epoll_add_event配合使用
        /*对照着上面提到的ngx_epoll_add_event方法,可以看到ptr成员就是ngx_connection_t连接的地址,但最后1位有特殊含义,需要把它屏蔽掉*/
        c = event_list[i].data.ptr; //通过这个确定是那个连接

        instance = (uintptr_t) c & 1;  //将地址的最后一位取出来,用instance变量标识, 见ngx_epoll_add_event
        /*无论是32位还是64位机器,其地址的最后1位肯定是0,可以用下面这行语句把ngx_connection_t的地址还原到真正的地址值*/

        //注意这里的c有可能是accept前的c,用于检测是否客户端发起tcp连接事件,accept返回成功后会重新创建一个ngx_connection_t,用来读写客户端的数据
        c = (ngx_connection_t *) ((uintptr_t) c & (uintptr_t) ~1);

        rev = c->read; //取出读事件 //注意这里的c有可能是accept前的c,用于检测是否客户端发起tcp连接事件,accept返回成功后会重新创建一个ngx_connection_t,用来读写客户端的数据

        if (c->fd == -1 || rev->instance != instance) { //判断这个读事件是否为过期事件
            //当fd套接字描述符为-l或者instance标志位不相等时,表示这个事件已经过期了,不用处理

            /*
             * the stale event from a file descriptor
             * that was just closed in this iteration
             */

            ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                           "epoll: stale event %p", c);
            continue;
        }

        revents = event_list[i].events; //取出事件类型

        ngx_log_debug3(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "epoll: fd:%d ev:%04XD d:%p",
                       c->fd, revents, event_list[i].data.ptr);

        if (revents & (EPOLLERR | EPOLLHUP)) { //例如对方close掉套接字,这里会感应到
            ngx_log_debug2(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                           "epoll_wait() error on fd:%d ev:%04XD",
                           c->fd, revents);

            /*
             * if the error events were returned, add EPOLLIN and EPOLLOUT
             * to handle the events at least in one active handler
             */

            revents |= EPOLLIN | EPOLLOUT; //epoll EPOLLERR|EPOLLHUP实际上是通过触发读写事件进行读写操作recv write来检测连接异常
        }

#if 0
        if (revents & ~(EPOLLIN|EPOLLOUT|EPOLLERR|EPOLLHUP)) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                          "strange epoll_wait() events fd:%d ev:%04XD",
                          c->fd, revents);
        }
#endif

        if ((revents & EPOLLIN) && rev->active) {  //如果是读事件且该事件是活跃的

#if (NGX_HAVE_EPOLLRDHUP)
            if (revents & EPOLLRDHUP) {
                rev->pending_eof = 1;
            }
#endif
            //注意这里的c有可能是accept前的c,用于检测是否客户端发起tcp连接事件,accept返回成功后会重新创建一个ngx_connection_t,用来读写客户端的数据
            rev->ready = 1; //表示已经有数据到了这里只是把accept成功前的 ngx_connection_t->read->ready置1,accept返回后会重新从连接池中获取一个ngx_connection_
            rev->available = -1;
            //flags参数中含有NGX_POST_EVENTS表示这批事件要延后处理
            if (flags & NGX_POST_EVENTS) {
                /*如果要在post队列中延后处理该事件,首先要判断它是新连接事件还是普通事件,以决定把它加入
                到ngx_posted_accept_events队列或者ngx_postedL events队列中.关于post队列中的事件何时执行*/
                queue = rev->accept ? &ngx_posted_accept_events
                                    : &ngx_posted_events;

                ngx_post_event(rev, queue);

            } else {
                //如果接收到客户端数据,这里为ngx_http_wait_request_handler
                rev->handler(rev); //如果为还没accept,则为ngx_event_process_init中设置为ngx_event_accept.如果已经建立连接,则读数据为ngx_http_process_request_line
            }
        }

        wev = c->write;

        if ((revents & EPOLLOUT) && wev->active) {

            if (c->fd == -1 || wev->instance != instance) { //判断这个读事件是否为过期事件
                //当fd套接字描述符为-1或者instance标志位不相等时,表示这个事件已经过期,不用处理

                /*
                 * the stale event from a file descriptor
                 * that was just closed in this iteration
                 */

                ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                               "epoll: stale event %p", c);
                continue;
            }

            wev->ready = 1;
#if (NGX_THREADS)
            wev->complete = 1;
#endif

            if (flags & NGX_POST_EVENTS) {
                ngx_post_event(wev, &ngx_posted_events);  //将这个事件添加到post队列中延后处理

            } else { //立即调用这个写事件的回调方法来处理这个事件
                wev->handler(wev);
            }
        }
    }

    return NGX_OK;
}


#if (NGX_HAVE_FILE_AIO)
/*异步文件I/O设置事件的回调方法为ngx_file_aio_event_handler,它的调用关系类似这样:epoll_wait(ngx_process_events_and_timers)中调用
ngx_epoll_eventfd_handler方法将当前事件放入到ngx_posted_events队列中,在延后执行的队列中调用ngx_file_aio_event_handler方法*/

/*整个网络事件的驱动机制就是这样通过ngx_eventfd通知描述符和ngx_epoll_eventfd
handler回调方法,并与文件异步I/O事件结合起来的.
    那么,怎样向异步I/O上下文中提交异步I/O操作呢?看看ngx_linux_aio read.c文件中
的ngx_file_aio_read方法,在打开文件异步I/O后,这个方法将会负责磁盘文件的读取*/

/*ngx_epoll_aio_init初始化aio事件列表, ngx_file_aio_read添加读文件事件,当读取完毕后epoll会触发
ngx_epoll_eventfd_handler->ngx_file_aio_event_handler
nginx file aio只提供了read接口,不提供write接口,因为异步aio只从磁盘读和写,而非aio方式一般写会落到
磁盘缓存,所以不提供该接口,如果异步io写可能会更慢*/

//该函数在ngx_process_events_and_timers中执行
static void
ngx_epoll_eventfd_handler(ngx_event_t *ev) //从epoll_wait中检测到aio读成功事件,则走到这里
{
    int               n, events;
    long              i;
    uint64_t          ready;
    ngx_err_t         err;
    ngx_event_t      *e;
    ngx_event_aio_t  *aio;
    struct io_event   event[64]; //一次性最多处理64个事件
    struct timespec   ts;

    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, ev->log, 0, "eventfd handler");
    //获取已经完成的事件数目,并设置到ready中,注意,这个ready是可以大于64的
    n = read(ngx_eventfd, &ready, 8); //调用read函数读取已完成的I/O的个数

    err = ngx_errno;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, ev->log, 0, "eventfd: %d", n);

    if (n != 8) {
        if (n == -1) {
            if (err == NGX_EAGAIN) {
                return;
            }

            ngx_log_error(NGX_LOG_ALERT, ev->log, err, "read(eventfd) failed");
            return;
        }

        ngx_log_error(NGX_LOG_ALERT, ev->log, 0,
                      "read(eventfd) returned only %d bytes", n);
        return;
    }

    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    //ready表示还未处理的事件.当ready大于0时继续处理
    while (ready) {
        //调用io_getevents获取已经完成的异步I/O事件
        events = io_getevents(ngx_aio_ctx, 1, 64, event, &ts);

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                       "io_getevents: %d", events);

        if (events > 0) {
            ready -= events; //将ready减去已经取出的事件

            for (i = 0; i < events; i++) { //处理event数组里的事件

                ngx_log_debug4(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                               "io_event: %XL %XL %L %L",
                                event[i].data, event[i].obj,
                                event[i].res, event[i].res2);
                //data成员指向这个异步I/O事件对应着的实际事件,这里的ngx_event_t为ngx_event_aio_t->event
               /*和ngx_epoll_eventfd_handler对应,先执行ngx_file_aio_read想异步I/O中添加读事件,然后通过epoll触发返回读取
               数据成功,再执行ngx_epoll_eventfd_handler*/
               //这里的e是在ngx_file_aio_read中添加进来的,在后面的ngx_post_event中会执行ngx_file_aio_event_handler
                e = (ngx_event_t *) (uintptr_t) event[i].data; //和ngx_file_aio_read中io_submit对应

                e->complete = 1;
                e->active = 0;
                e->ready = 1;

                aio = e->data;
                aio->res = event[i].res;

                ngx_post_event(e, &ngx_posted_events);
                //将该事件放到ngx_posted_events队列中延后执行,执行该队列的handle地方见ngx_process_events_and_timers
            }

            continue;
        }

        if (events == 0) {
            return;
        }

        /* events == -1 */
        ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
                      "io_getevents() failed");
        return;
    }
}

#endif


static void *
ngx_epoll_create_conf(ngx_cycle_t *cycle) {
    ngx_epoll_conf_t *epcf;

    epcf = ngx_palloc(cycle->pool, sizeof(ngx_epoll_conf_t));
    if (epcf == NULL) {
        return NULL;
    }

    epcf->events = NGX_CONF_UNSET;
    epcf->aio_requests = NGX_CONF_UNSET;

    return epcf;
}


static char *
ngx_epoll_init_conf(ngx_cycle_t *cycle, void *conf) {
    ngx_epoll_conf_t *epcf = conf;

    ngx_conf_init_uint_value(epcf->events, 512);
    ngx_conf_init_uint_value(epcf->aio_requests, 32);

    return NGX_CONF_OK;
}
