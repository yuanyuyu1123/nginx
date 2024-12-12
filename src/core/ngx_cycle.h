
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_CYCLE_H_INCLUDED_
#define _NGX_CYCLE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


#ifndef NGX_CYCLE_POOL_SIZE
#define NGX_CYCLE_POOL_SIZE     NGX_DEFAULT_POOL_SIZE
#endif


#define NGX_DEBUG_POINTS_STOP   1
#define NGX_DEBUG_POINTS_ABORT  2


typedef struct ngx_shm_zone_s ngx_shm_zone_t;

typedef ngx_int_t (*ngx_shm_zone_init_pt)(ngx_shm_zone_t *zone, void *data);
//在ngx_http_upstream_cache_get中获取zone的时候获取的是fastcgi_cache proxy_cache设置的zone,因此必须配置fastcgi_cache (proxy_cache) abc;中的xxx和xxx_cache_path(proxy_cache_path fastcgi_cache_path) xxx keys_zone=abc:10m;一致
//所有的共享内存都通过ngx_http_file_cache_s->shpool进行管理   每个共享内存对应一个ngx_slab_pool_t来管理,见ngx_init_zone_pool
struct ngx_shm_zone_s {  //初始化见ngx_shared_memory_add,真正的共享内存创建在ngx_init_cycle->ngx_init_cycle
    void *data; //指向ngx_http_file_cache_t,赋值见ngx_http_file_cache_set_slot
    ngx_shm_t shm; //ngx_init_cycle->ngx_shm_alloc->ngx_shm_alloc中创建相应的共享内存空间
    //ngx_init_cycle中执行
    ngx_shm_zone_init_pt init; // "zone" proxy_cache_path fastcgi_cache_path等配置中设置为ngx_http_file_cache_init   ngx_http_upstream_init_zone
    void *tag; //创建的这个共享内存属于哪个模块
    void *sync;
    ngx_uint_t noreuse;  /* unsigned  noreuse:1; */
};


//http://tech.uc.cn/?p=300 参数解析相关数据结构参考
//初始化参考ngx_init_cycle,最终有一个全局类型的ngx_cycle_s,即ngx_cycle,  ngx_conf_s中包含该类型成员cycle
struct ngx_cycle_s {
    /*保存着所有模块存储配置项的结构体指针,它首先是一个数组,数组大小为ngx_max_module,正好与Nginx的module个数一样；
     每个数组成员又是一个指针,指向另一个存储着指针的数组,因此会看到void **** */

    /*例如http核心模块的conf_ctx[ngx_http_module->index]=ngx_http_conf_ctx_t,见ngx_conf_handler,ngx_http_block
    见ngx_init_cycle  conf.ctx = cycle->conf_ctx; //这样下面的ngx_conf_param解析配置的时候,里面对conf.ctx赋值操作,实际上就是对cycle->conf_ctx[i]
    可如何由ngx_cycle_t核心结构体中找到main级别的配置结构体呢？Nginx提供的ngx_http_cycle_get_module_main_conf宏可以实现这个功能*/
    void ****conf_ctx; //有多少个模块就会有多少个指向这些模块的指针,见ngx_init_cycle   ngx_max_module
    ngx_pool_t *pool; // 内存池
    /*日志模块中提供了生成基本ngx_log_t日志对象的功能,这里的log实际上是在还没有执行ngx_init_cycle方法前,
    也就是还没有解析配置前,如果有信息需要输出到日志,就会暂时使用log对象,它会输出到屏幕.
    在ngx_init_cycle方法执行后,将会根据nginx.conf配置文件中的配置项,构造出正确的日志文件,此时会对log重新赋值.*/
    //ngx_init_cycle中赋值cycle->log = &cycle->new_log;
    ngx_log_t *log; //指向ngx_log_init中的ngx_log,如果配置error_log,指向这个配置后面的文件参数,见ngx_error_log.否则在ngx_log_open_default中设置
    /* 由nginx.conf配置文件读取到日志文件路径后,将开始初始化error_log日志文件,由于log对象还在用于输出日志到屏幕,
    这时会用new_log对象暂时性地替代log日志,待初始化成功后,会用new_log的地址覆盖上面的log指针*/
    // 如果没有配置error_log则在ngx_log_open_default设置为NGX_ERROR_LOG_PATH,如果通过error_log有配置过则通过ngx_log_set_log添加到该new_log->next链表连接起来
    /* 全局中配置的error_log xxx存储在ngx_cycle_s->new_log,http{}、server{}、local{}配置的error_log保存在ngx_http_core_loc_conf_t->error_log,
    见ngx_log_set_log,如果只配置全局error_log,不配置http{}、server{}、local{}则在ngx_http_core_merge_loc_conf conf->error_log = &cf->cycle->new_log;*/
    //ngx_log_insert插入,在ngx_log_error_core找到对应级别的日志配置进行输出,因为可以配置error_log不同级别的日志存储在不同的日志文件中
    ngx_log_t new_log; //如果配置error_log,指向这个配置后面的文件参数,见ngx_error_log.否则在ngx_log_open_default中设置

    ngx_uint_t log_use_stderr;  /* unsigned  log_use_stderr:1; */
    /* 对于poll,rtsig这样的事件模块,会以有效文件句柄数来预先建立这些ngx_connection t结构
      体,以加速事件的收集、分发.这时files就会保存所有ngx_connection_t的指针组成的数组,files_n就是指
      针的总数,而文件句柄的值用来访问files数组成员 */
    ngx_connection_t **files; //sizeof(ngx_connection_t *) * cycle->files_n  见ngx_event_process_init  ngx_get_connection
    /*从图9-1中可以看出,在ngx_cycle_t中的connections和free_connections达两个成员构成了一个连接池,其中connections指向整个连
    接池数组的首部,而free_connections则指向第一个ngx_connection_t空闲连接.所有的空闲连接ngx_connection_t都以data成员（见9.3.1节）作
    为next指针串联成一个单链表,如此,一旦有用户发起连接时就从free_connections指向的链表头获取一个空闲的连接,同时free_connections再指
    向下一个空闲连接.而归还连接时只需把该连接插入到free_connections链表表头即可.*/
    //见ngx_event_process_init, ngx_connection_t空间和它当中的读写ngx_event_t存储空间都在该函数一次性分配好
    ngx_connection_t *free_connections; //可用连接池,与free_connection_n配合使用
    ngx_uint_t free_connection_n; //可用连接池中连接的总数

    ngx_module_t **modules;
    ngx_uint_t modules_n;
    ngx_uint_t modules_used;    /* unsigned  modules_used:1; */
    //ngx_connection_s中的queue添加到该链表上
    /*通过读操作可以判断连接是否正常,如果不正常的话,就会把该ngx_close_connection->ngx_free_connection释放出来,这样
    如果之前free_connections上没有空余ngx_connection_t,c = ngx_cycle->free_connections;就可以获取到刚才释放出来的ngx_connection_t
    见ngx_drain_connections*/
    ngx_queue_t reusable_connections_queue; /* 双向链表容器,元素类型是ngx_connection_t结构体,表示可重复使用连接队列 表示可以重用的连接 */
    ngx_uint_t reusable_connections_n;
    time_t connections_reuse_time;
    //ngx_http_optimize_servers->ngx_http_init_listening->ngx_http_add_listening->ngx_create_listening把解析到的listen配置项信息添加到cycle->listening中
    //通过"listen"配置创建ngx_listening_t加入到该数组中
    //注意,有多少个worker进程就会复制多少个ngx_listening_t, 见ngx_clone_listening
    ngx_array_t listening; // 动态数组,每个数组元素储存着ngx_listening_t成员,表示监听端口及相关的参数
    /*动态数组容器,它保存着nginx所有要操作的目录.如果有目录不存在,就会试图创建,而创建目录失败就会导致nginx启动失败*/
    //通过解析配置文件获取到的路径添加到该数组,例如nginx.conf中的client_body_temp_path proxy_temp_path,参考ngx_conf_set_path_slot
    //这些配置可能设置重复的路径,因此不需要重复创建,通过ngx_add_path检测添加的路径是否重复,不重复则添加到paths中
    ngx_array_t paths; //数组成员 nginx_path_t,该数组在ngx_init_cycle中预分配空间

    ngx_array_t config_dump; //该数组在ngx_init_cycle中预分配空间
    ngx_rbtree_t config_dump_rbtree;
    ngx_rbtree_node_t config_dump_sentinel;
    /*单链表容器,元素类型是ngx_open_file_t 结构体,它表示nginx已经打开的所有文件.
      事实上,nginx框架不会向open_files链表中添加文件.
      而是由对此感兴趣的模块向其中添加文件路径名,nginx框架会在ngx_init_cycle 方法中打开这些文件 ,先通过ngx_conf_open_file设置好数组文件,然后
      在ngx_init_cycle真正创建文件*/
    //access_log error_log配置的文件信息都通过ngx_conf_open_file加入到open_files链表中
    //该链表中所包含的文件的打开在ngx_init_cycle中打开
    ngx_list_t open_files; //如nginx.conf配置文件中的access_log参数的文件就保存在该链表中,参考ngx_conf_open_file
    //真正的共享内存空间创建ngx_shm_zone_t在ngx_init_cycle,需要创建哪些共享内存,由配置文件指定,然后调用
    //ngx_shared_memory_add把这些信息保存到shared_memory链表,ngx_init_cycle解析完配置文件后进行共享内存真正的统一分配
    ngx_list_t shared_memory; // 单链表容器,元素类型是ngx_shm_zone_t结构体,每个元素表示一块共享内存
    //最开始free_connection_n=connection_n,见ngx_event_process_init
    ngx_uint_t connection_n; // 当前进程中所有链接对象的总数,与成员配合使用
    ngx_uint_t files_n; //每个进程能够打开的最多文件数  赋值见ngx_event_process_init
    /*从图9-1中可以看出,在ngx_cycle_t中的connections和free_connections达两个成员构成了一个连接池,其中connections指向整个连接池数组的首部,
    而free_connections则指向第一个ngx_connection_t空闲连接.所有的空闲连接ngx_connection_t都以data成员（见9.3.1节）作为next指针串联成一个
    单链表,如此,一旦有用户发起连接时就从free_connections指向的链表头获取一个空闲的连接,同时free_connections再指向下一个空闲连
    接.而归还连接时只需把该连接插入到free_connections链表表头即可.
    在connections指向的连接池中,每个连接所需要的读/写事件都以相同的数组序号对应着read_events、write_events读/写事件数组,
    相同序号下这3个数组中的元素是配合使用的*/
    //子进程在ngx_event_process_init中创建空间和赋值,connections和read_events  write_events数组对应
    ngx_connection_t *connections; // 指向当前进程中的所有连接对象,与connection_n配合使用
    /*事件是不需要创建的,因为Nginx在启动时已经在ngx_cycle_t的read_events成员中预分配了所有的读事件,并在write_events成员中预分配了所有的写事件
   在connections指向的连接池中,每个连接所需要的读/写事件都以相同的数组序号对应着read_events、write_events读/写事件数组,相同序号下这
   3个数组中的元素是配合使用的.图9-1中还显示了事件池,Nginx认为每一个连接一定至少需要一个读事件和一个写事件,有多少连接就分配多少个读、
   写事件.怎样把连接池中的任一个连接与读事件、写事件对应起来呢？很简单.由于读事件、写事件、连接池是由3个大小相同的数组组成,所以根据数组
   序号就可将每一个连接、读事件、写事件对应起来,这个对应关系在ngx_event_core_module模块的初始化过程中就已经决定了（参见9.5节）.这3个数组
   的大小都是由cycle->connection_n决定.*/
    //预分配的读写事件空间,类型ngx_event_t  //子进程在ngx_event_process_init中创建空间和赋值,connections和read_events  write_events数组对应
    ngx_event_t *read_events; // 指向当前进程中的所有读事件对象,connection_n同时表示所有读事件的总数,因为每个连接分别有一个读写事件
    ngx_event_t *write_events; // 指向当前进程中的所有写事件对象,connection_n同时表示所有写事件的总数,因为每个连接分别有一个读写事件
    /*旧的ngx_cycle_t 对象用于引用上一个ngx_cycle_t 对象中的成员.例如ngx_init_cycle 方法,在启动初期,
    需要建立一个临时的ngx_cycle_t对象保存一些变量,
    再调用ngx_init_cycle 方法时就可以把旧的ngx_cycle_t 对象传进去,而这时old_cycle对象就会保存这个前期的ngx_cycle_t对象*/
    ngx_cycle_t *old_cycle;

    ngx_str_t conf_file; // 配置文件相对于安装目录的路径名称 默认为安装路径下的NGX_CONF_PATH,见ngx_process_options
    ngx_str_t conf_param; // nginx 处理配置文件时需要特殊处理的在命令行携带的参数,一般是-g 选项携带的参数
    ngx_str_t conf_prefix; // nginx配置文件所在目录的路径  ngx_prefix 见ngx_process_options
    ngx_str_t prefix; //nginx安装目录的路径 ngx_prefix 见ngx_process_options
    ngx_str_t error_log;
    ngx_str_t lock_file; // 用于进程间同步的文件锁名称
    ngx_str_t hostname; // 使用gethostname系统调用得到的主机名  在ngx_init_cycle中大写字母被转换为小写字母
};


typedef struct { //从ngx_cycle_s->conf_ctx[ngx_core_module.index]指向这里
    ngx_flag_t daemon;
    ngx_flag_t master; //在ngx_core_module_init_conf中初始化为1  通过参数"master_process"设置

    ngx_msec_t timer_resolution; //从timer_resolution全局配置中解析到的参数,表示多少ms执行定时器中断,然后epoll_wail会返回跟新内存时间
    ngx_msec_t shutdown_timeout;

    ngx_int_t worker_processes; //创建的worker进程数,通过nginx配置,默认为1  "worker_processes"设置
    ngx_int_t debug_points;
    //修改工作进程的打开文件数的最大值限制(RLIMIT_NOFILE),用于在不重启主进程的情况下增大该限制
    ngx_int_t rlimit_nofile;
    //修改工作进程的core文件尺寸的最大值限制(RLIMIT_CORE),用于在不重启主进程的情况下增大该限制.
    off_t rlimit_core; //worker_rlimit_core 1024k;  coredump文件大小

    int priority;

    ngx_uint_t cpu_affinity_auto;
    /*
     worker_processes 4;
     worker_cpu_affinity 0001 0010 0100 1000; 四个工作进程分别在四个指定的he上面运行

     如果是5he可以这样配置
     worker_cpu_affinity 00001 00010 00100 01000 10000; 其他多核类似*/
    //参考ngx_set_cpu_affinity
    ngx_uint_t cpu_affinity_n; //worker_cpu_affinity参数个数
    ngx_cpuset_t *cpu_affinity; //worker_cpu_affinity 00001 00010 00100 01000 10000;转换的位图结果就是0X11111

    char *username;
    ngx_uid_t user;
    ngx_gid_t group;

    ngx_str_t working_directory; //working_directory /var/yyz/corefile/;  coredump存放路径
    ngx_str_t lock_file;

    ngx_str_t pid; //默认NGX_PID_PATH,主进程名
    ngx_str_t oldpid; //NGX_PID_PATH+NGX_OLDPID_EXT  热升级nginx进程的时候用

    ngx_array_t env; //成员类型ngx_str_t,见ngx_core_module_create_conf
    char **environment; //直接指向env,见ngx_set_environment

    ngx_uint_t transparent;  /* unsigned  transparent:1; */
} ngx_core_conf_t;


#define ngx_is_init_cycle(cycle)  (cycle->conf_ctx == NULL)


ngx_cycle_t *ngx_init_cycle(ngx_cycle_t *old_cycle);

ngx_int_t ngx_create_pidfile(ngx_str_t *name, ngx_log_t *log);

void ngx_delete_pidfile(ngx_cycle_t *cycle);

ngx_int_t ngx_signal_process(ngx_cycle_t *cycle, char *sig);

void ngx_reopen_files(ngx_cycle_t *cycle, ngx_uid_t user);

char **ngx_set_environment(ngx_cycle_t *cycle, ngx_uint_t *last);

ngx_pid_t ngx_exec_new_binary(ngx_cycle_t *cycle, char *const *argv);

ngx_cpuset_t *ngx_get_cpu_affinity(ngx_uint_t n);

ngx_shm_zone_t *ngx_shared_memory_add(ngx_conf_t *cf, ngx_str_t *name,
                                      size_t size, void *tag);

void ngx_set_shutdown_timer(ngx_cycle_t *cycle);


extern volatile ngx_cycle_t *ngx_cycle;
extern ngx_array_t ngx_old_cycles;
extern ngx_module_t ngx_core_module;
extern ngx_uint_t ngx_test_config;
extern ngx_uint_t ngx_dump_config;
extern ngx_uint_t ngx_quiet_mode;


#endif /* _NGX_CYCLE_H_INCLUDED_ */
