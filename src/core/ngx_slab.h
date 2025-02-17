
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_SLAB_H_INCLUDED_
#define _NGX_SLAB_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

/*nginx的请求分布在不同的进程,如果进程间需要交互各种不同大小的对象,需要共享一些复杂的数据结构,
   如链表、树、图等,nginx实现了一套高效的slab内存管理机制,可以帮助我们快速实现多种对象间的跨Nginx worker进程通信*/

typedef struct ngx_slab_page_s ngx_slab_page_t;

struct ngx_slab_page_s { //初始化赋值在ngx_slab_init
    /*多种情况,多个用途:
        如果obj<64,一个页中存放的是多个obj(例如64个64字节obj),则slab记录里面的obj的大小,见ngx_slab_alloc_locked
        如果obj移位大小为ngx_slab_exact_size,也就是obj=64byte,page->slab = 1;page->slab存储obj的bitmap,例如这里为1,表示说第一个obj分配出去了,见ngx_slab_alloc_locked
        如果obj移位大小为ngx_slab_exact_shift,也就是obj>64byte,page->slab = ((uintptr_t) 1 << NGX_SLAB_MAP_SHIFT) | shift;
        大于64,也就是至少128,4K最多也就32个128,因此只需要slab的高16位表示obj位图即可
    当分配某些大小的obj的时候(一个缓存页存放多个obj),slab表示被分配的缓存的占用情况(是否空闲),以bit位来表示*/
    uintptr_t slab; //ngx_slab_init中初始赋值为共享内存中剩余页的个数

    /*在ngx_slab_init中初始化的9个ngx_slab_page_s通过next连接在一起,
    如果页中的obj<64则next直接指向对应的页slots[slot].next = page; 同时pages_m[]指向page->next = &slots[slot];*/
    ngx_slab_page_t *next; //在分配较小obj的时候,next指向slab page在pool->pages的位置,下一个page页

    /*由于指针是4的倍数,那么后两位一定为0,此时我们可以利用指针的后两位做标记,充分利用空间.
    在nginx的slab中,我们使用ngx_slab_page_s结构体中的指针pre的后两位做标记,用于指示该page页面的slot块数与ngx_slab_exact_size的关系:
    当page划分的slot块小于32时候,pre的后两位为NGX_SLAB_SMALL.
    当page划分的slot块等于32时候,pre的后两位为NGX_SLAB_EXACT
    当page划分的slot大于32块时候,pre的后两位为NGX_SLAB_BIG
    当page页面不划分slot时候,即将整个页面分配给用户,pre的后两位为NGX_SLAB_PAGE  */
    uintptr_t prev; //上一个page页
};

typedef struct {
    ngx_uint_t total;
    ngx_uint_t used;

    ngx_uint_t reqs;
    ngx_uint_t fails;
} ngx_slab_stat_t;

/*共享内存的其实地址开始处数据:ngx_slab_pool_t + 9 * sizeof(ngx_slab_page_t)(slots_m[]) + pages * sizeof(ngx_slab_page_t)(pages_m[]) +pages*ngx_pagesize
(这是实际的数据部分,每个ngx_pagesize都由前面的一个ngx_slab_page_t进行管理,并且每个ngx_pagesize最前端第一个obj存放的是一个或者多个int类型bitmap,用于管理每块分配出去的内存)
m_slot[0]:链接page页面,并且page页面划分的slot块大小为2^3
m_slot[1]:链接page页面,并且page页面划分的slot块大小为2^4
m_slot[2]:链接page页面,并且page页面划分的slot块大小为2^5
……………….
m_slot[8]:链接page页面,并且page页面划分的slot块大小为2k(2048)
m_page数组:数组中每个元素对应一个page页.
m_page[0]对应page[0]页面
m_page[1]对应page[1]页面
m_page[2]对应page[2]页面
…………………………
m_page[k]对应page[k]页面
另外可能有的m_page[]没有相应页面与他相对应*/
typedef struct { //初始化赋值在ngx_slab_init,slab结构是配合共享内存使用的,可以以limit_req模块为例,参考ngx_http_limit_req_module
    ngx_shmtx_sh_t lock;  //mutex的锁

    size_t min_size; //内存缓存obj最小的大小,一般是1个byte,最小分配的空间是8byte,见ngx_slab_init

    //slab pool以shift来比较和计算所需分配的obj大小、每个缓存页能够容纳obj个数以及所分配的页在缓存空间的位置
    size_t min_shift;  //ngx_init_zone_pool中默认为3

    //每一页对应一个ngx_slab_page_t,所有的ngx_slab_page_t存放在连续的内存中组成数组,pages就是数组首地址
    ngx_slab_page_t *pages;  //slab page空间的开头,初始指向pages * sizeof(ngx_slab_page_t)首地址
    ngx_slab_page_t *last; // 也就是指向实际的数据页pages*ngx_pagesize,指向最后一个pages页

    //管理free的页面是一个链表头,用于连接空闲页面.
    ngx_slab_page_t free; //初始化赋值在ngx_slab_init,free->next指向pages * sizeof(ngx_slab_page_t),下次从free.next是下次分配页时候的入口开始分配页空间

    ngx_slab_stat_t *stats;
    ngx_uint_t pfree;

    u_char *start; //所有的实际页面全部连续地放在一起,start指向这个连续的内存的首地址
    u_char *end; // 指向这个连续地共享内存的末地址

    ngx_shmtx_t mutex;  //ngx_init_zone_pool->ngx_shmtx_create->sem_init进行初始化

    u_char *log_ctx; //操作失败时会记录日志,为区别是哪个slab共享内存出错,可以在slab中分配一段内存存放描述的字符串,然后再用log_ctx指向这个字符串；
    u_char zero; //实际就是'\0',当log_ctx没有赋值时,将直接指向zero,表示空字符串防止出错；

    unsigned log_nomem: 1;  //ngx_slab_init中默认为1
    //ngx_http_file_cache_init中cache->shpool->data = cache->sh;
    void *data; //由各个使用slab的模块自由使用,slab管理内存时不会用到它
    void *addr; //指向所属的ngx_shm_zone_t里的ngx_shm_t成员的addr成员,一般用于指示一段共享内存块的起始位置
} ngx_slab_pool_t;


void ngx_slab_sizes_init(void);

void ngx_slab_init(ngx_slab_pool_t *pool);

void *ngx_slab_alloc(ngx_slab_pool_t *pool, size_t size);

void *ngx_slab_alloc_locked(ngx_slab_pool_t *pool, size_t size);

void *ngx_slab_calloc(ngx_slab_pool_t *pool, size_t size);

void *ngx_slab_calloc_locked(ngx_slab_pool_t *pool, size_t size);

void ngx_slab_free(ngx_slab_pool_t *pool, void *p);

void ngx_slab_free_locked(ngx_slab_pool_t *pool, void *p);


#endif /* _NGX_SLAB_H_INCLUDED_ */
