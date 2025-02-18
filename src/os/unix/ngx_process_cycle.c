
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_channel.h>


static void ngx_start_worker_processes(ngx_cycle_t *cycle, ngx_int_t n,
                                       ngx_int_t type);

static void ngx_start_cache_manager_processes(ngx_cycle_t *cycle,
                                              ngx_uint_t respawn);

static void ngx_pass_open_channel(ngx_cycle_t *cycle);

static void ngx_signal_worker_processes(ngx_cycle_t *cycle, int signo);

static ngx_uint_t ngx_reap_children(ngx_cycle_t *cycle);

static void ngx_master_process_exit(ngx_cycle_t *cycle);

static void ngx_worker_process_cycle(ngx_cycle_t *cycle, void *data);

static void ngx_worker_process_init(ngx_cycle_t *cycle, ngx_int_t worker);

static void ngx_worker_process_exit(ngx_cycle_t *cycle);

static void ngx_channel_handler(ngx_event_t *ev);

static void ngx_cache_manager_process_cycle(ngx_cycle_t *cycle, void *data);

static void ngx_cache_manager_process_handler(ngx_event_t *ev);

static void ngx_cache_loader_process_handler(ngx_event_t *ev);


//如果是第一次加载,则满足ngx_is_init_cycle.如果是reload热启动,则原来的nginx进程的ngx_process == NGX_PROCESS_MASTER
ngx_uint_t    ngx_process;//默认是NGX_PROCESS_SINGLE
ngx_uint_t    ngx_worker;
ngx_pid_t     ngx_pid;//ngx_pid = ngx_getpid(); 在子进程中为子进程pid,在master中为master的pid
ngx_pid_t ngx_parent;
sig_atomic_t ngx_reap;
sig_atomic_t ngx_sigio;
sig_atomic_t ngx_sigalrm;
sig_atomic_t ngx_terminate; //当接收到TERM信号时,ngx_terminate标志位会设为1,这是在告诉worker进程需要强制关闭进程;
sig_atomic_t ngx_quit; //当接收到QUIT信号时,ngx_quit标志位会设为1,这是在告诉worker进程需要优雅地关闭进程;
sig_atomic_t ngx_debug_quit;
ngx_uint_t ngx_exiting; //ngx_exiting标志位仅由ngx_worker_process_cycle方法在退出时作为标志位使用
sig_atomic_t ngx_reconfigure; //nginx -s reload会触发该新号
sig_atomic_t ngx_reopen;  //当接收到USRI信号时,ngx_reopen标志位会设为1,这是在告诉Nginx需要重新打开文件(如切换日志文件时)

sig_atomic_t ngx_change_binary; //平滑升级到新版本的Nginx程序,热升级
ngx_pid_t ngx_new_binary; //进行热代码替换,这里是调用execve来执行新的代码. 这个是在ngx_change_binary的基础上获取值
ngx_uint_t ngx_inherited;
ngx_uint_t ngx_daemonized;

sig_atomic_t ngx_noaccept;
ngx_uint_t ngx_noaccepting;
ngx_uint_t ngx_restart;


static u_char master_process[] = "master process";

/*在Nginx中,如果启用了proxy(fastcgi) cache功能,master process会在启动的时候启动管理缓存的两个子进程(区别于处理请求的子进程)来管理内存和磁盘的缓存个体.
 * 第一个进程的功能是定期检查缓存,并将过期的缓存删除;第二个进程的作用是在启动的时候将磁盘中已经缓存的个体映射到内存中(目前Nginx设定为启动以后60秒),然后退出.
具体的,在这两个进程的ngx_process_events_and_timers()函数中,会调用ngx_event_expire_timers().Nginx的ngx_event_timer_rbtree(红黑树)里
面按照执行的时间的先后存放着一系列的事件.每次取执行时间最早的事件,如果当前时间已经到了应该执行该事件,就会调用事件的handler.
两个进程的handler分别是ngx_cache_manager_process_handler和ngx_cache_loader_process_handler
也就是说manger 和 loader的定时器会分别调用ngx_cache_manager_process_handler和ngx_cache_loader_process_handler,不过可以看到manager的定
时器初始时间是0,而loader是60000毫秒.也就是说,manager在nginx一启动时就启动了,但是,loader是在nginx启动了1分钟后才会启动.*/
static ngx_cache_manager_ctx_t ngx_cache_manager_ctx = {
        ngx_cache_manager_process_handler, "cache manager process", 0
};

static ngx_cache_manager_ctx_t ngx_cache_loader_ctx = {
        ngx_cache_loader_process_handler, "cache loader process", 60000 //进程创建后60000m秒执行ngx_cache_loader_process_handler,在ngx_cache_manager_process_cycle中添加的定时器
};


static ngx_cycle_t ngx_exit_cycle;
static ngx_log_t ngx_exit_log;
static ngx_open_file_t ngx_exit_log_file;

/*ngx_master_process_cycle调用ngx_start_worker_processes生成多个工作子进程,ngx_start_worker_processes调用ngx_worker_process_cycle
创建工作内容,如果进程有多个子线程,这里也会初始化线程和创建线程工作内容,初始化完成之后,ngx_worker_process_cycle
会进入处理循环,调用 ngx_process_events_and_timers,该函数调用ngx_process_events监听事件,
并把事件投递到事件队列ngx_posted_events中,最终会在ngx_event_thread_process_posted中处理事件.*/

/*master进程不需要处理网络事件,它不负责业务的执行,只会通过管理worker等子进
程来实现重启服务、平滑升级、更换日志文件、配置文件实时生效等功能*/

//如果是多进程方式启动,就会调用ngx_master_process_cycle完成最后的启动动作
void
ngx_master_process_cycle(ngx_cycle_t *cycle) {
    char *title;
    u_char *p;
    size_t size;
    ngx_int_t i;
    ngx_uint_t sigio;
    sigset_t set;
    struct itimerval itv;
    ngx_uint_t live;
    ngx_msec_t delay;
    ngx_core_conf_t *ccf;

    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    sigaddset(&set, SIGALRM);
    sigaddset(&set, SIGIO);
    sigaddset(&set, SIGINT);
    sigaddset(&set, ngx_signal_value(NGX_RECONFIGURE_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_REOPEN_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_NOACCEPT_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_TERMINATE_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_CHANGEBIN_SIGNAL));
    /*每个进程有一个信号掩码(signal mask).简单地说,信号掩码是一个"位图",其中每一位都对应着一种信号
    如果位图中的某一位为1,就表示在执行当前信号的处理程序期间相应的信号暂时被"屏蔽",使得在执行的过程中不会嵌套地响应那种信号.

    为什么对某一信号进行屏蔽呢?我们来看一下对CTRL_C的处理.大家知道,当一个程序正在运行时,在键盘上按一下CTRL_C,内核就会向相应的进程
    发出一个SIGINT 信号,而对这个信号的默认操作就是通过do_exit()结束该进程的运行.但是,有些应用程序可能对CTRL_C有自己的处理,所以就要
    为SIGINT另行设置一个处理程序,使它指向应用程序中的一个函数,在那个函数中对CTRL_C这个事件作出响应.但是,在实践中却发现,两次CTRL_C
    事件往往过于密集,有时候刚刚进入第一个信号的处理程序,第二个SIGINT信号就到达了,而第二个信号的默认操作是杀死进程,这样,第一个信号
    的处理程序根本没有执行完.为了避免这种情况的出现,就在执行一个信号处理程序的过程中将该种信号自动屏蔽掉.所谓"屏蔽",与将信号忽略
    是不同的,它只是将信号暂时"遮盖"一下,一旦屏蔽去掉,已到达的信号又继续得到处理,不会丢失.*/

    // 设置这些信号都阻塞,等我们sigpending调用才告诉我有这些事件
    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1) { //参考下面的sigsuspend
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "sigprocmask() failed");
    }

    sigemptyset(&set);


    size = sizeof(master_process);

    for (i = 0; i < ngx_argc; i++) {
        size += ngx_strlen(ngx_argv[i]) + 1;
    }

    title = ngx_pnalloc(cycle->pool, size);
    if (title == NULL) {
        /* fatal */
        exit(2);
    }
    /* 把master process + 参数一起组成主进程名 */
    p = ngx_cpymem(title, master_process, sizeof(master_process) - 1);
    for (i = 0; i < ngx_argc; i++) {
        *p++ = ' ';
        p = ngx_cpystrn(p, (u_char *) ngx_argv[i], size);
    }

    ngx_setproctitle(title); //修改进程名为title


    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    ngx_start_worker_processes(cycle, ccf->worker_processes,
                               NGX_PROCESS_RESPAWN); //启动worker进程
    ngx_start_cache_manager_processes(cycle, 0); //启动cache manager, cache loader进程

    ngx_new_binary = 0;
    delay = 0;
    sigio = 0;
    live = 1;
    /*每次一个循环执行完毕后进程会被挂起,直到有新的信号才会激活继续执行)*/
    for (;;) {
        /*delay用来等待子进程退出的时间,由于我们接受到SIGINT信号后,我们需要先发送信号给子进程,而子进程的退出需要一定的时间,
        超时时如果子进程已退出,我们父进程就直接退出,否则发送sigkill信号给子进程(强制退出),然后再退出.*/
        if (delay) {
            if (ngx_sigalrm) {
                sigio = 0;
                delay *= 2;
                ngx_sigalrm = 0;
            }

            ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                           "termination cycle: %M", delay);

            itv.it_interval.tv_sec = 0;
            itv.it_interval.tv_usec = 0;
            itv.it_value.tv_sec = delay / 1000;
            itv.it_value.tv_usec = (delay % 1000) * 1000;

            //设置定时器,以系统真实时间来计算,送出SIGALRM信号,这个信号反过来会设置ngx_sigalrm为1,这样delay就会不断翻倍.
            if (setitimer(ITIMER_REAL, &itv, NULL) == -1) { //每隔itv时间发送一次SIGALRM信号
                ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                              "setitimer() failed");
            }
        }

        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "sigsuspend");
        /*sigsuspend(const sigset_t *mask))用于在接收到某个信号之前, 临时用mask替换进程的信号掩码, 并暂停进程执行,直到收到信号为止.
         sigsuspend 返回后将恢复调用之前的信号掩码.信号处理函数完成后,进程将继续执行.该系统调用始终返回-1,并将errno设置为EINTR.

         其实sigsuspend是一个原子操作,包含4个步骤:
         (1) 设置新的mask阻塞当前进程;
         (2) 收到信号,恢复原先mask;
         (3) 调用该进程设置的信号处理函数;
         (4) 待信号处理函数返回后,sigsuspend返回. */

        /*等待信号发生,前面sigprocmask后有设置sigemptyset(&set);所以这里会等待接收所有信号,只要有信号到来则返回.例如定时信号,ngx_reap,ngx_terminate等信号;
        从上面的(2)步骤可以看出在处理函数中执行信号中断函数的,由于这时候已经恢复了原来的mask(也就是上面sigprocmask设置的掩码集)
        所以在信号处理函数中不会再次引起接收信号,只能在该while()循环再次走到sigsuspend的时候引起信号中断,从而避免了同一时刻多次中断同一信号*/

        sigsuspend(&set); //等待定时器超时,通过ngx_init_signals执行ngx_signal_handler中的SIGALRM信号,信号处理函数返回后,继续该函数后面的操作

        ngx_time_update();

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "wake up, sigio %i", sigio);

        if (ngx_reap) { //父进程收到一个子进程退出的信号,见ngx_signal_handler
            ngx_reap = 0;
            ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "reap children");
            //这个里面处理退出的子进程(有的worker异常退出,这时我们就需要重启这个worker ),如果所有子进程都退出则会返回0.
            live = ngx_reap_children(cycle); // 有子进程意外结束,这时需要监控所有的子进程,也就是ngx_reap_children方法所做的工作
        }
        //如果没有存活的子进程,并且收到了ngx_terminate或者ngx_quit信号,则master退出.
        if (!live && (ngx_terminate || ngx_quit)) {
            ngx_master_process_exit(cycle);
        }
        /*如果ngx_terminate标志位为l,则向所有子进程发送信号TERM．通知子进程强制退出进程,接下来直接跳到第1步并挂起进程,等待信号激活进程.*/
        if (ngx_terminate) { //收到了sigint信号.
            if (delay == 0) {
                delay = 50;//设置延时
            }

            if (sigio) {
                sigio--;
                continue;
            }

            sigio = ccf->worker_processes + 2 /* cache processes */;

            if (delay > 1000) { //如果超时,则强制杀死worker
                ngx_signal_worker_processes(cycle, SIGKILL);
            } else {  //负责发送sigint给worker,让它退出.
                ngx_signal_worker_processes(cycle,
                                            ngx_signal_value(NGX_TERMINATE_SIGNAL));
            }

            continue;
        }
        /*继续ngx_quit为1的分支流程.关闭所有的监听端口,接下来直接跳到第1步并挂起master进程,等待信号激活进程.*/
        if (ngx_quit) { //收到quit信号.
            //发送给worker quit信号
            ngx_signal_worker_processes(cycle,
                                        ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
            ngx_close_listening_sockets(cycle);

            continue;
        }

        /*如果ngx_reconfigure标志位为0,则跳到第13步检查ngx_restart标志位.如果ngx_reconfigure为l,则表示需要重新读取配置文件.
         Nginx不会再让原先的worker等子进程再重新读取配置文件,它的策略是重新初始化ngx_cycle_t结构体,用它来读取新的配置文件,
         再拉起新的worker进程,销毁旧的worker进程.本步中将会调用ngx_init_cycle方法重新初始化ngx_cycle_t结构体.*/
        if (ngx_reconfigure) {  //重读配置文件并使服务对新配置项生效
            ngx_reconfigure = 0;

            if (ngx_new_binary) {  //判断是否热代码替换后的新的代码还在运行中(也就是还没退出当前的master).如果还在运行中,则不需要重新初始化config
                ngx_start_worker_processes(cycle, ccf->worker_processes,
                                           NGX_PROCESS_RESPAWN);
                ngx_start_cache_manager_processes(cycle, 0);
                ngx_noaccepting = 0;

                continue;
            }

            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reconfiguring");

            cycle = ngx_init_cycle(cycle);  //重新初始化config,并重新启动新的worker
            if (cycle == NULL) {
                cycle = (ngx_cycle_t *) ngx_cycle;
                continue;
            }

            ngx_cycle = cycle;
            ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx,
                                                   ngx_core_module);
            //调用ngx_start_worker_processes方法再拉起一批worker进程,这些worker进程将使用新ngx_cycle_t绪构体.
            ngx_start_worker_processes(cycle, ccf->worker_processes,
                                       NGX_PROCESS_JUST_RESPAWN);
            //调用ngx_start_cache_manager_processes方法,按照缓存模块的加载情况决定是否拉起cache manage或者cache loader进程.
            //在这两个方法调用后,肯定是存在子进程了,这时会把live标志位置为1
            ngx_start_cache_manager_processes(cycle, 1);

            /* allow new processes to start */
            ngx_msleep(100);

            live = 1;
            //向原先的(并非刚刚拉起的）所有子进程发送QUIT信号,要求它们优雅地退出自己的进程
            ngx_signal_worker_processes(cycle,
                                        ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
        }

        if (ngx_restart) {
            ngx_restart = 0;
            ngx_start_worker_processes(cycle, ccf->worker_processes,
                                       NGX_PROCESS_RESPAWN);
            ngx_start_cache_manager_processes(cycle, 0);
            live = 1;
        }
        /*使用-s reopen参数可以重新打开日志文件,这样可以先把当前日志文件改名或转移到其他目录中进行备份,再重新打开时就会生成新的日志文件.
        这个功能使得日志文件不至于过大.当然,这与使用kill命令发送USR1信号效果相同.*/
        if (ngx_reopen) {
            /*如果ngx_reopen为1,则调用ngx_reopen_files方法重新打开所有文件,同时将ngx_reopen标志位置为0.
                向所有子进程发送USRI信号,要求子进程都得重新打开所有文件.*/
            ngx_reopen = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reopening logs");
            ngx_reopen_files(cycle, ccf->user);
            ngx_signal_worker_processes(cycle,
                                        ngx_signal_value(NGX_REOPEN_SIGNAL));
        }
        /*检查ngx_change_binary标志位,如果ngx_change_binary为1,则表示需要平滑升级Nginx,这时将调用ngx_exec_new_binary方法用新的子
        进程启动新版本的Nginx程序, 同时将ngx_change_binary标志位置为0. */
        if (ngx_change_binary) { //平滑升级到新版本的Nginx程序
            ngx_change_binary = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "changing binary");
            ngx_new_binary = ngx_exec_new_binary(cycle, ngx_argv); //进行热代码替换,这里是调用execve来执行新的代码.
        }
        //接受到停止accept连接,其实也就是worker退出(有区别的是,这里master不需要退出)

        /*检查ngx_noaccept标志位,如果ngx_noaccept为0,则继续第1步进行下一个循环:如果ngx_noacicept为1,则向所有的子进程发送QUIT信号,
          要求它们优雅地关闭服务,同时将ngx_noaccept置为0,并将ngx_noaccepting置为1,表示正在停止接受新的连接.*/
        if (ngx_noaccept) { //所有子进程不再接受处理新的连接,实际相当于对所有的予进程发送QUIT信号量
            ngx_noaccept = 0;
            ngx_noaccepting = 1;
            //给worker发送信号.
            ngx_signal_worker_processes(cycle,
                                        ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
        }
    }
}

/*如果nginx.conf中配置为单进程工作模式,这时将会调用ngx_single_process_cycle方法进入单迸程工作模式.*/
void
ngx_single_process_cycle(ngx_cycle_t *cycle) {
    ngx_uint_t i;

    if (ngx_set_environment(cycle, NULL) == NULL) {
        /* fatal */
        exit(2);
    }

    for (i = 0; cycle->modules[i]; i++) {
        if (cycle->modules[i]->init_process) {
            if (cycle->modules[i]->init_process(cycle) == NGX_ERROR) {
                /* fatal */
                exit(2);
            }
        }
    }

    for (;;) {
        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "worker cycle");

        ngx_process_events_and_timers(cycle);

        if (ngx_terminate || ngx_quit) {

            for (i = 0; cycle->modules[i]; i++) {
                if (cycle->modules[i]->exit_process) {
                    cycle->modules[i]->exit_process(cycle);
                }
            }

            ngx_master_process_exit(cycle);
        }

        if (ngx_reconfigure) {
            ngx_reconfigure = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reconfiguring");

            cycle = ngx_init_cycle(cycle);
            if (cycle == NULL) {
                cycle = (ngx_cycle_t *) ngx_cycle;
                continue;
            }

            ngx_cycle = cycle;
        }

        if (ngx_reopen) {
            ngx_reopen = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reopening logs");
            ngx_reopen_files(cycle, (ngx_uid_t) -1);
        }
    }
}


static void
ngx_start_worker_processes(ngx_cycle_t *cycle, ngx_int_t n, ngx_int_t type) {
    ngx_int_t i;

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "start worker processes");

    for (i = 0; i < n; i++) {
        /*
                                 |----------(ngx_worker_process_cycle->ngx_worker_process_init)
    ngx_start_worker_processes---| ngx_processes[]相关的操作赋值流程
                                 |----------ngx_pass_open_channel
    */
        ngx_spawn_process(cycle, ngx_worker_process_cycle,
                          (void *) (intptr_t) i, "worker process", type);

        ngx_pass_open_channel(cycle);
    }
}


//多子进程环境下会调用该函数生成两个管理缓存的进程
static void
ngx_start_cache_manager_processes(ngx_cycle_t *cycle, ngx_uint_t respawn) {
    ngx_uint_t i, manager, loader;
    ngx_path_t **path;

    manager = 0;
    loader = 0;

    path = ngx_cycle->paths.elts;
    for (i = 0; i < ngx_cycle->paths.nelts; i++) {

        if (path[i]->manager) {
            manager = 1;
        }

        if (path[i]->loader) {
            loader = 1;
        }
    }
    /*在这一步骤中,由master进程根据之前各模块的初始化情况来决定是否启动cachemanage子进程,也就是根据ngx_cycle_t中存储路径的动态数组
    pathes申是否有某个路径的manage标志位打开来决定是否启动cache manage子进程.如果有任何1个路径的manage标志位为1,则启动cache manage子进程.*/
    if (manager == 0) { //只有在配置了缓存信息才会置1,所以如果没有配置缓存不会启动cache manage和load进程
        return;
    }

    ngx_spawn_process(cycle, ngx_cache_manager_process_cycle,
                      &ngx_cache_manager_ctx, "cache manager process",
                      respawn ? NGX_PROCESS_JUST_RESPAWN : NGX_PROCESS_RESPAWN);

    ngx_pass_open_channel(cycle);
    /*如果有任何1个路径的loader标志位为1,则启动cache loader子进程, 与文件缓存模块密切相关*/
    if (loader == 0) {
        return;
    }

    ngx_spawn_process(cycle, ngx_cache_manager_process_cycle,
                      &ngx_cache_loader_ctx, "cache loader process",
                      respawn ? NGX_PROCESS_JUST_SPAWN : NGX_PROCESS_NORESPAWN);

    ngx_pass_open_channel(cycle);
}

/*子进程创建的时候,父进程的东西都会被子进程继承,所以后面创建的子进程能够得到前面创建的子进程的channel信息,直接可以和他们通信,
那么前面创建的进程如何知道后面的进程信息呢? 很简单,既然前面创建的进程能够接受消息,那么我就发个信息告诉他后面的进程
的channel,并把信息保存在channel[0]中,这样就可以相互通信了.*/
static void
ngx_pass_open_channel(ngx_cycle_t *cycle) { //该函数可以建立本进程和其他所有子进程的通道关系,和父进程的通道关系是直接继承过来的,所以本进程可以通过ch->fd和所有的
    ngx_int_t i;
    ngx_channel_t ch;

    ngx_memzero(&ch, sizeof(ngx_channel_t));

    ch.command = NGX_CMD_OPEN_CHANNEL; //传递给其他worker子进程的命令,打开通信管道
    ch.pid = ngx_processes[ngx_process_slot].pid;
    ch.slot = ngx_process_slot;
    ch.fd = ngx_processes[ngx_process_slot].channel[0];
    /*由master进程按照配置文件中worker进程的数目,启动这些子进程(也就是调用ngx_start_worker_processes方法）.*/
    for (i = 0; i < ngx_last_process; i++) { /* ngx_last_process全局变量,同样在ngx_spawn_process()中被赋值,意为最后面的进程 */
        // 跳过刚创建的worker子进程 || 不存在的子进程 || 其父进程socket关闭的子进程
        //可以和ngx_worker_process_init中的channel关闭操作配合阅读
        if (i == ngx_process_slot
            || ngx_processes[i].pid == -1
            || ngx_processes[i].channel[0] == -1) {
            continue;
        }

        ngx_log_debug6(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                       "pass channel s:%i pid:%P fd:%d to s:%i pid:%P fd:%d",
                       ch.slot, ch.pid, ch.fd,
                       i, ngx_processes[i].pid,
                       ngx_processes[i].channel[0]);

        /* TODO: NGX_AGAIN */
        //发送消息给其他的worker
        /* 给每个进程的父进程发送刚创建worker进程的信息,IPC方式以后再搞 */

        //可以和ngx_worker_process_init中的channel关闭操作;配合阅读向每个进程channel[0]发送信息

        //对于父进程而言,他知道所有进程的channel[0], 直接可以向子进程发送命令.

        ngx_write_channel(ngx_processes[i].channel[0],
                          &ch, sizeof(ngx_channel_t), cycle->log); //ch为本进程信息,ngx_processes[i].channel[0]为其他进程信息
    }
}

/*NGX_PROCESS_JUST_RESPAWN标识最终会在ngx_spawn_process()创建worker进程时,将ngx_processes[s].just_spawn = 1,以此作为区别旧的worker进程的标记.
 * 之后执行:ngx_signal_worker_processes(cycle, ngx_signal_value(NGX_SHUTDOWN_SIGNAL));以此关闭旧的worker进程.
 * 进入该函数,你会发现它也是循环向所有worker进程发送信号,所以它会先把旧worker进程关闭,然后再管理新的worker进程.*/
static void  //ngx_reap_children和ngx_signal_worker_processes对应
ngx_signal_worker_processes(ngx_cycle_t *cycle, int signo) { //向进程发送signo信号
    ngx_int_t i;
    ngx_err_t err;
    ngx_channel_t ch;

    ngx_memzero(&ch, sizeof(ngx_channel_t));

#if (NGX_BROKEN_SCM_RIGHTS)

    ch.command = 0;

#else

    switch (signo) {

        case ngx_signal_value(NGX_SHUTDOWN_SIGNAL):
            ch.command = NGX_CMD_QUIT;
            break;

        case ngx_signal_value(NGX_TERMINATE_SIGNAL):
            ch.command = NGX_CMD_TERMINATE;
            break;

        case ngx_signal_value(NGX_REOPEN_SIGNAL):
            ch.command = NGX_CMD_REOPEN;
            break;

        default:
            ch.command = 0;
    }

#endif

    ch.fd = -1;


    for (i = 0; i < ngx_last_process; i++) {

        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "child: %i %P e:%d t:%d d:%d r:%d j:%d",
                       i,
                       ngx_processes[i].pid,
                       ngx_processes[i].exiting,
                       ngx_processes[i].exited,
                       ngx_processes[i].detached,
                       ngx_processes[i].respawn,
                       ngx_processes[i].just_spawn);

        if (ngx_processes[i].detached || ngx_processes[i].pid == -1) {
            continue;
        }

        if (ngx_processes[i].just_spawn) {
            ngx_processes[i].just_spawn = 0;
            continue;
        }

        if (ngx_processes[i].exiting
            && signo == ngx_signal_value(NGX_SHUTDOWN_SIGNAL)) {
            continue;
        }

        if (ch.command) {
            if (ngx_write_channel(ngx_processes[i].channel[0],
                                  &ch, sizeof(ngx_channel_t), cycle->log)
                == NGX_OK) {
                if (signo != ngx_signal_value(NGX_REOPEN_SIGNAL)) {
                    ngx_processes[i].exiting = 1;
                }

                continue;
            }
        }

        ngx_log_debug2(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                       "kill (%P, %d)", ngx_processes[i].pid, signo);

        if (kill(ngx_processes[i].pid, signo) == -1) {
            err = ngx_errno;
            ngx_log_error(NGX_LOG_ALERT, cycle->log, err,
                          "kill(%P, %d) failed", ngx_processes[i].pid, signo);

            if (err == NGX_ESRCH) {
                ngx_processes[i].exited = 1;
                ngx_processes[i].exiting = 0;
                ngx_reap = 1;
            }

            continue;
        }

        if (signo != ngx_signal_value(NGX_REOPEN_SIGNAL)) {
            ngx_processes[i].exiting = 1;
        }
    }
}

//这个里面处理退出的子进程(有的worker异常退出,这时我们就需要重启这个worker),如果所有子进程都退出则会返回0.
static ngx_uint_t
ngx_reap_children(ngx_cycle_t *cycle) { //ngx_reap_children和ngx_signal_worker_processes对应
    ngx_int_t i, n;
    ngx_uint_t live;
    ngx_channel_t ch;
    ngx_core_conf_t *ccf;

    ngx_memzero(&ch, sizeof(ngx_channel_t));

    ch.command = NGX_CMD_CLOSE_CHANNEL;
    ch.fd = -1;

    live = 0;
    for (i = 0; i < ngx_last_process; i++) {

        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "child: %i %P e:%d t:%d d:%d r:%d j:%d",
                       i,
                       ngx_processes[i].pid,
                       ngx_processes[i].exiting,
                       ngx_processes[i].exited,
                       ngx_processes[i].detached,
                       ngx_processes[i].respawn,
                       ngx_processes[i].just_spawn);

        if (ngx_processes[i].pid == -1) {
            continue;
        }

        if (ngx_processes[i].exited) {

            if (!ngx_processes[i].detached) {
                ngx_close_channel(ngx_processes[i].channel, cycle->log);

                ngx_processes[i].channel[0] = -1;
                ngx_processes[i].channel[1] = -1;

                ch.pid = ngx_processes[i].pid;
                ch.slot = i;

                for (n = 0; n < ngx_last_process; n++) {
                    if (ngx_processes[n].exited
                        || ngx_processes[n].pid == -1
                        || ngx_processes[n].channel[0] == -1) {
                        continue;
                    }

                    ngx_log_debug3(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                                   "pass close channel s:%i pid:%P to:%P",
                                   ch.slot, ch.pid, ngx_processes[n].pid);

                    /* TODO: NGX_AGAIN */

                    ngx_write_channel(ngx_processes[n].channel[0],
                                      &ch, sizeof(ngx_channel_t), cycle->log);
                }
            }

            if (ngx_processes[i].respawn
                && !ngx_processes[i].exiting
                && !ngx_terminate
                && !ngx_quit) {
                if (ngx_spawn_process(cycle, ngx_processes[i].proc,
                                      ngx_processes[i].data,
                                      ngx_processes[i].name, i)
                    == NGX_INVALID_PID) {
                    ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                                  "could not respawn %s",
                                  ngx_processes[i].name);
                    continue;
                }


                ngx_pass_open_channel(cycle);

                live = 1;

                continue;
            }

            if (ngx_processes[i].pid == ngx_new_binary) {

                ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx,
                                                       ngx_core_module);

                if (ngx_rename_file((char *) ccf->oldpid.data,
                                    (char *) ccf->pid.data)
                    == NGX_FILE_ERROR) {
                    ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                                  ngx_rename_file_n " %s back to %s failed "
                                                    "after the new binary process \"%s\" exited",
                                  ccf->oldpid.data, ccf->pid.data, ngx_argv[0]);
                }

                ngx_new_binary = 0;
                if (ngx_noaccepting) {
                    ngx_restart = 1;
                    ngx_noaccepting = 0;
                }
            }

            if (i == ngx_last_process - 1) {
                ngx_last_process--;

            } else {
                ngx_processes[i].pid = -1;
            }

        } else if (ngx_processes[i].exiting || !ngx_processes[i].detached) {
            live = 1;
        }
    }

    return live;
}


static void
ngx_master_process_exit(ngx_cycle_t *cycle) {
    ngx_uint_t i;

    ngx_delete_pidfile(cycle);

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "exit");

    for (i = 0; cycle->modules[i]; i++) {
        if (cycle->modules[i]->exit_master) {
            cycle->modules[i]->exit_master(cycle);
        }
    }

    ngx_close_listening_sockets(cycle);

    /*
     * Copy ngx_cycle->log related data to the special static exit cycle,
     * log, and log file structures enough to allow a signal handler to log.
     * The handler may be called when standard ngx_cycle->log allocated from
     * ngx_cycle->pool is already destroyed.
     */


    ngx_exit_log = *ngx_log_get_file_log(ngx_cycle->log);

    ngx_exit_log_file.fd = ngx_exit_log.file->fd;
    ngx_exit_log.file = &ngx_exit_log_file;
    ngx_exit_log.next = NULL;
    ngx_exit_log.writer = NULL;

    ngx_exit_cycle.log = &ngx_exit_log;
    ngx_exit_cycle.files = ngx_cycle->files;
    ngx_exit_cycle.files_n = ngx_cycle->files_n;
    ngx_cycle = &ngx_exit_cycle;

    ngx_destroy_pool(cycle->pool);

    exit(0);
}


//在Nginx主循环(这里的主循环是ngx_worker_process_cycle方法）中,会定期地调用事件模块,以检查是否有网络事件发生.
static void
ngx_worker_process_cycle(ngx_cycle_t *cycle, void *data) { //data表示这是第几个worker进程
    ngx_int_t worker = (intptr_t) data;  //worker表示绑定到第几个cpu上

    ngx_process = NGX_PROCESS_WORKER;
    ngx_worker = worker;

    ngx_worker_process_init(cycle, worker);  //主要工作是把CPU和进程绑定

    ngx_setproctitle("worker process");
    /*在ngx_worker_process_cycle有法中,通过检查ngx_exiting、ngx_terminate、ngx_quit、ngx_reopen这4个标志位来决定后续动作*/
    for (;;) {

        if (ngx_exiting) {
            if (ngx_event_no_timers_left() == NGX_OK) {
                ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "exiting");
                ngx_worker_process_exit(cycle);
            }
        }

        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "worker cycle");

        ngx_process_events_and_timers(cycle);
        //没有关闭套接字,也没有处理为处理完的事件,而是直接exit
        if (ngx_terminate) {
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "exiting");
            ngx_worker_process_exit(cycle);
        }

        if (ngx_quit) {
            ngx_quit = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                          "gracefully shutting down");
            ngx_setproctitle("worker process is shutting down");

            if (!ngx_exiting) {
                /*如果ngx_exiting为1,则开始准备关闭worker进程.首先,根据当前ngx_cycle_t中所有正在处理的连接,调用它们对应的关闭连接处理方法
                   (就是将连接中的close标志位置为1,再调用读事件的处理方法,在第9章中会详细讲解Nginx连接）.调用所有活动连接的读事件处理方法处
                   理连接关闭事件后,将检查ngx_event timer_ rbtree红黑树(保存所有事件的定时器,在第9章中会介绍它）是否为空,如果不为空,表示还
                   有事件需要处理,将继续向下执行,调用ngx_process_events_and_timers方法处理事件;如果为空,表示已经处理完所有的事件,这时将调
                   用所有模块的exit_process方法,最后销毁内存池,退出整个worker进程.
               注意ngx_exiting标志位只有唯一一段代码会设置它,也就是下面接收到QUIT信号.ngx_quit只有;在首次设置为1时,才会将ngx_exiting置为1.*/
                ngx_exiting = 1; //开始quit后的相关资源释放操作,见上面的if(ngx_exting)
                ngx_set_shutdown_timer(cycle);
                ngx_close_listening_sockets(cycle);
                ngx_close_idle_connections(cycle);
            }
        }
        /*使用-s reopen参数可以重新打开日志文件,这样可以先把当前日志文件改名或转移到其他目录中进行备份,再重新打开时就会生成新的日志文件.
         这个功能使得日志文件不至于过大.当然,这与使用kill命令发送USR1信号效果相同*/
        if (ngx_reopen) {
            ngx_reopen = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reopening logs");
            ngx_reopen_files(cycle, -1);
        }
    }
}

/*
                                 |----------(ngx_worker_process_cycle->ngx_worker_process_init)
    ngx_start_worker_processes---| ngx_processes[]相关的操作赋值流程
                                 |----------ngx_pass_open_channel
*/
static void
ngx_worker_process_init(ngx_cycle_t *cycle, ngx_int_t worker) { //主要工作是把CPU和进程绑定  创建epoll_crate等
    sigset_t set;
    ngx_int_t n;
    ngx_time_t *tp;
    ngx_uint_t i;
    ngx_cpuset_t *cpu_affinity;
    struct rlimit rlmt;
    ngx_core_conf_t *ccf;
    ngx_listening_t *ls;

    if (ngx_set_environment(cycle, NULL) == NULL) {
        /* fatal */
        exit(2);
    }

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    if (worker >= 0 && ccf->priority != 0) {
        if (setpriority(PRIO_PROCESS, 0, ccf->priority) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setpriority(%d) failed", ccf->priority);
        }
    }

    if (ccf->rlimit_nofile != NGX_CONF_UNSET) {
        rlmt.rlim_cur = (rlim_t) ccf->rlimit_nofile;
        rlmt.rlim_max = (rlim_t) ccf->rlimit_nofile;
        //RLIMIT_NOFILE指定此进程可打开的最大文件描述词大一的值,超出此值,将会产生EMFILE错误.
        if (setrlimit(RLIMIT_NOFILE, &rlmt) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setrlimit(RLIMIT_NOFILE, %i) failed",
                          ccf->rlimit_nofile);
        }
    }

    if (ccf->rlimit_core != NGX_CONF_UNSET) {
        rlmt.rlim_cur = (rlim_t) ccf->rlimit_core;
        rlmt.rlim_max = (rlim_t) ccf->rlimit_core;
        //修改工作进程的core文件尺寸的最大值限制(RLIMIT_CORE),用于在不重启主进程的情况下增大该限制.
        if (setrlimit(RLIMIT_CORE, &rlmt) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setrlimit(RLIMIT_CORE, %O) failed",
                          ccf->rlimit_core);
        }
    }

    if (geteuid() == 0) {
        if (setgid(ccf->group) == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "setgid(%d) failed", ccf->group);
            /* fatal */
            exit(2);
        }

        if (initgroups(ccf->username, ccf->group) == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "initgroups(%s, %d) failed",
                          ccf->username, ccf->group);
        }

#if (NGX_HAVE_PR_SET_KEEPCAPS && NGX_HAVE_CAPABILITIES)
        if (ccf->transparent && ccf->user) {
            if (prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0) == -1) {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                              "prctl(PR_SET_KEEPCAPS, 1) failed");
                /* fatal */
                exit(2);
            }
        }
#endif

        if (setuid(ccf->user) == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "setuid(%d) failed", ccf->user);
            /* fatal */
            exit(2);
        }

#if (NGX_HAVE_CAPABILITIES)
        if (ccf->transparent && ccf->user) {
            struct __user_cap_data_struct data;
            struct __user_cap_header_struct header;

            ngx_memzero(&header, sizeof(struct __user_cap_header_struct));
            ngx_memzero(&data, sizeof(struct __user_cap_data_struct));

            header.version = _LINUX_CAPABILITY_VERSION_1;
            data.effective = CAP_TO_MASK(CAP_NET_RAW);
            data.permitted = data.effective;

            if (syscall(SYS_capset, &header, &data) == -1) {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                              "capset() failed");
                /* fatal */
                exit(2);
            }
        }
#endif
    }

    if (worker >= 0) {
        cpu_affinity = ngx_get_cpu_affinity(worker);

        if (cpu_affinity) {
            ngx_setaffinity(cpu_affinity, cycle->log);
        }
    }

#if (NGX_HAVE_PR_SET_DUMPABLE)

    /* allow coredump after setuid() in Linux 2.4.x */

    if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "prctl(PR_SET_DUMPABLE) failed");
    }

#endif

    if (ccf->working_directory.len) { //路径必须存在,否则返回错误
        if (chdir((char *) ccf->working_directory.data) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "chdir(\"%s\") failed", ccf->working_directory.data);
            /* fatal */
            exit(2);
        }
    }

    sigemptyset(&set);

    if (sigprocmask(SIG_SETMASK, &set, NULL) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "sigprocmask() failed");
    }

    tp = ngx_timeofday();
    srandom(((unsigned) ngx_pid << 16) ^ tp->sec ^ tp->msec);

    /*
     * disable deleting previous events for the listening sockets because
     * in the worker processes there are no events at all at this point
     */
    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {
        ls[i].previous = NULL;
    }

    for (i = 0; cycle->modules[i]; i++) {
        if (cycle->modules[i]->init_process) {
            if (cycle->modules[i]->init_process(cycle) == NGX_ERROR) {
                /* fatal */
                exit(2);
            }
        }
    }
    /*

   用socketpair生成两个sock[0]和sock[1]用于父进程和子进程的通信,当父进程使用其中一个socket时,为什么要调用close,关闭子进程的sock[1],代码如下:
         int r = socketpair( AF_UNIX, SOCK_STREAM, 0, fd );
         if ( fork() ) {
             Parent process: echo client
             int val = 0;
             close( fd[1] );
              while ( 1 ) {
               sleep( 1 );
               ++val;
               printf( "Sending data: %d\n", val );
               write( fd[0], &val, sizeof(val) );
               read( fd[0], &val, sizeof(val) );
               printf( "Data received: %d\n", val );
             }
           }
           else {
              Child process: echo server
             int val;
             close( fd[0] );
             while ( 1 ) {
               read( fd[1], &val, sizeof(val) );
               ++val;
               write( fd[1], &val, sizeof(val) );
             }
           }
         }
   ------Solutions------
   调用socketpair创建的两个socket都是打开的,fork后子进程会继承这两个打开的socket.为了实现父子进程通过socket pair(类似于管道）通信,必须保证父子进程分别open某一个socket.
   ------Solutions------
   可以这么理解:父子进程一个负责向socket写数据,一个从中读取数据,当写的时候当然不能读了,同理,当读的时候就不能写了.和操作系统中的临界资源差不多.

   channel[0] 是用来发送信息的,channel[1]是用来接收信息的.那么对自己而言,它需要向其他进程发送信息,需要保留其它进程的channel[0],
   关闭channel[1]; 对自己而言,则需要关闭channel[0]. 最后把ngx_channel放到epoll中,从第一部分中的介绍我们可以知道,这个ngx_channel
   实际就是自己的 channel[1].这样有信息进来的时候就可以通知到了.*/

    //关闭所有其它子进程对应的 channel[1] 和 自己的 channel[0].从而实现子进程的channel[1]和主进程的channel[0]通信
    for (n = 0; n < ngx_last_process; n++) {

        if (ngx_processes[n].pid == -1) {
            continue;
        }

        if (n == ngx_process_slot) {
            continue;
        }

        if (ngx_processes[n].channel[1] == -1) {
            continue;
        }

        if (close(ngx_processes[n].channel[1]) == -1) { //关闭除本进程以外的其他所有进程的读端
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "close() channel failed");
        }
    }

    if (close(ngx_processes[ngx_process_slot].channel[0]) == -1) { //关闭本进程的写端,剩下的一条通道还是全双工的
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "close() channel failed");
    }

#if 0
    ngx_last_process = 0;
#endif
    //调用epoll add 把ngx_chanel 加入epoll 中
    if (ngx_add_channel_event(cycle, ngx_channel, NGX_READ_EVENT,
                              ngx_channel_handler) //在ngx_spawn_process中赋值
        == NGX_ERROR) {
        /* fatal */
        exit(2);
    }
}


static void
ngx_worker_process_exit(ngx_cycle_t *cycle) {
    ngx_uint_t i;
    ngx_connection_t *c;

    for (i = 0; cycle->modules[i]; i++) {
        if (cycle->modules[i]->exit_process) {
            cycle->modules[i]->exit_process(cycle);
        }
    }

    if (ngx_exiting) {
        c = cycle->connections;
        for (i = 0; i < cycle->connection_n; i++) {
            if (c[i].fd != -1
                && c[i].read
                && !c[i].read->accept
                && !c[i].read->channel
                && !c[i].read->resolver) {
                ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                              "*%uA open socket #%d left in connection %ui",
                              c[i].number, c[i].fd, i);
                ngx_debug_quit = 1;
            }
        }

        if (ngx_debug_quit) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0, "aborting");
            ngx_debug_point();
        }
    }

    /*
     * Copy ngx_cycle->log related data to the special static exit cycle,
     * log, and log file structures enough to allow a signal handler to log.
     * The handler may be called when standard ngx_cycle->log allocated from
     * ngx_cycle->pool is already destroyed.
     */

    ngx_exit_log = *ngx_log_get_file_log(ngx_cycle->log);

    ngx_exit_log_file.fd = ngx_exit_log.file->fd;
    ngx_exit_log.file = &ngx_exit_log_file;
    ngx_exit_log.next = NULL;
    ngx_exit_log.writer = NULL;

    ngx_exit_cycle.log = &ngx_exit_log;
    ngx_exit_cycle.files = ngx_cycle->files;
    ngx_exit_cycle.files_n = ngx_cycle->files_n;
    ngx_cycle = &ngx_exit_cycle;

    ngx_destroy_pool(cycle->pool);

    ngx_log_error(NGX_LOG_NOTICE, ngx_cycle->log, 0, "exit");

    exit(0);
}

/*而在子进程中是如何处理的呢,子进程的管道可读事件捕捉函数是ngx_channel_handler(ngx_event_t *ev),在这个函数中,会读取mseeage,然后解析,并根据不同的命令做不同的处理*/

//和ngx_write_channel对应
static void
ngx_channel_handler(ngx_event_t *ev) {
    ngx_int_t n;
    ngx_channel_t ch;
    ngx_connection_t *c;

    if (ev->timedout) {
        ev->timedout = 0;
        return;
    }

    c = ev->data;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, ev->log, 0, "channel handler");

    for (;;) {

        n = ngx_read_channel(c->fd, &ch, sizeof(ngx_channel_t), ev->log);

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0, "channel: %i", n);

        if (n == NGX_ERROR) {

            if (ngx_event_flags & NGX_USE_EPOLL_EVENT) {
                ngx_del_conn(c, 0);
            }

            ngx_close_connection(c);
            return;
        }

        if (ngx_event_flags & NGX_USE_EVENTPORT_EVENT) {
            if (ngx_add_event(ev, NGX_READ_EVENT, 0) == NGX_ERROR) {
                return;
            }
        }

        if (n == NGX_AGAIN) {
            return;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0,
                       "channel command: %ui", ch.command);

        switch (ch.command) {

            case NGX_CMD_QUIT:
                ngx_quit = 1;
                break;

            case NGX_CMD_TERMINATE:
                ngx_terminate = 1;
                break;

            case NGX_CMD_REOPEN:
                ngx_reopen = 1;
                break;

            case NGX_CMD_OPEN_CHANNEL:

                ngx_log_debug3(NGX_LOG_DEBUG_CORE, ev->log, 0,
                               "get channel s:%i pid:%P fd:%d",
                               ch.slot, ch.pid, ch.fd);
                //对ngx_processes全局进程表进行赋值.
                ngx_processes[ch.slot].pid = ch.pid;
                ngx_processes[ch.slot].channel[0] = ch.fd;
                break;

            case NGX_CMD_CLOSE_CHANNEL:

                ngx_log_debug4(NGX_LOG_DEBUG_CORE, ev->log, 0,
                               "close channel s:%i pid:%P our:%P fd:%d",
                               ch.slot, ch.pid, ngx_processes[ch.slot].pid,
                               ngx_processes[ch.slot].channel[0]);

                if (close(ngx_processes[ch.slot].channel[0]) == -1) {
                    ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
                                  "close() channel failed");
                }

                ngx_processes[ch.slot].channel[0] = -1;
                break;
        }
    }
}

/*除了充当代理服务器,nginx还可行使类似varnish/squid的缓存职责,即将客户端的请求内容缓存在Nginx服务器,下次同样的请求则由nginx直接返回,
减轻了被代理服务器的压力;cache使用一块公共内存区域(共享内存）,存放缓存的索引数据,Nginx启动时cache loader进程将磁盘缓存的对象文件
(cycle->pathes,以红黑树组织)加载到内存中,加载完毕后自动退出;只有开启了proxy buffer才能使用proxy cache;
注:若被代理服务器返回的http头包含no-store/no-cache/private/max-age=0或者expires包含过期日期时,则该响应数据不被nginx缓存;*/

//后端应答数据在ngx_http_upstream_process_request->ngx_http_file_cache_update中进行缓存
static void
ngx_cache_manager_process_cycle(ngx_cycle_t *cycle, void *data) { //nginx: cache loader process进程和nginx: cache manager process都会执行该函数
    ngx_cache_manager_ctx_t *ctx = data;

    void *ident[4];
    ngx_event_t ev;

    /*
     * Set correct process type since closing listening Unix domain socket
     * in a master process also removes the Unix domain socket file.
     */
    ngx_process = NGX_PROCESS_HELPER;

    ngx_close_listening_sockets(cycle);

    /* Set a moderate number of connections for a helper process. */
    cycle->connection_n = 512;

    ngx_worker_process_init(cycle, -1);

    ngx_memzero(&ev, sizeof(ngx_event_t));
    ev.handler = ctx->handler;
    ev.data = ident;
    ev.log = cycle->log;
    ident[3] = (void *) -1;

    ngx_use_accept_mutex = 0;

    ngx_setproctitle(ctx->name);

    ngx_add_timer(&ev, ctx->delay);

    for (;;) {

        if (ngx_terminate || ngx_quit) {
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "exiting");
            exit(0);
        }

        if (ngx_reopen) {
            ngx_reopen = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reopening logs");
            ngx_reopen_files(cycle, -1);
        }

        ngx_process_events_and_timers(cycle);
    }
}


static void
ngx_cache_manager_process_handler(ngx_event_t *ev) {
    ngx_uint_t i;
    ngx_msec_t next, n;
    ngx_path_t **path;

    next = 60 * 60 * 1000;

    path = ngx_cycle->paths.elts;
    for (i = 0; i < ngx_cycle->paths.nelts; i++) {

        if (path[i]->manager) {
            n = path[i]->manager(path[i]->data);

            next = (n <= next) ? n : next;

            ngx_time_update();
        }
    }

    if (next == 0) {
        next = 1;
    }

    ngx_add_timer(ev, next);
}


static void
ngx_cache_loader_process_handler(ngx_event_t *ev) {
    ngx_uint_t i;
    ngx_path_t **path;
    ngx_cycle_t *cycle;

    cycle = (ngx_cycle_t *) ngx_cycle;

    path = cycle->paths.elts;
    for (i = 0; i < cycle->paths.nelts; i++) {

        if (ngx_terminate || ngx_quit) {
            break;
        }

        if (path[i]->loader) {
            path[i]->loader(path[i]->data);
            ngx_time_update();
        }
    }

    exit(0);
}
