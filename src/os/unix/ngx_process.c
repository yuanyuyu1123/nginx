
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_channel.h>

//信号发送见ngx_os_signal_process
typedef struct {
    int     signo;   //需要处理的信号
    char   *signame; //信号对应的字符串名称
    char   *name;    //这个信号对应着的Nginx命令

    void (*handler)(int signo, siginfo_t *siginfo, void *ucontext); //收到signo信号后就会回调handler方法
} ngx_signal_t;


static void ngx_execute_proc(ngx_cycle_t *cycle, void *data);

static void ngx_signal_handler(int signo, siginfo_t *siginfo, void *ucontext);

static void ngx_process_get_status(void);

static void ngx_unlock_mutexes(ngx_pid_t pid);


int ngx_argc;
char **ngx_argv; //存放执行nginx时候所带的参数, 见ngx_save_argv

char **ngx_os_argv; //指向nginx运行时候所带的参数,见ngx_save_argv

//当前操作的进程在ngx_processes数组中的下标
ngx_int_t ngx_process_slot;

//存储所有子进程的数组,ngx_spawn_process中赋值:  ngx_channel = ngx_processes[s].channel[1]
ngx_socket_t ngx_channel;

//ngx_processes数组中有意义的ngx_process_t元素中最大的下标
ngx_int_t ngx_last_process;

/*在解释master工作流程前,还需要对master进程管理子进程的数据结构有个初步了解.下面定义了pgx_processes全局数组,虽然子进程中也会
有ngx_processes数组,但这个数组仅仅是给master进程使用的*/
ngx_process_t ngx_processes[NGX_MAX_PROCESSES];  //存储所有子进程的数组  ngx_spawn_process中赋值

//信号发送见ngx_os_signal_process 信号处理在ngx_signal_handler
ngx_signal_t signals[] = {
        {ngx_signal_value(NGX_RECONFIGURE_SIGNAL),
                  "SIG" ngx_value(NGX_RECONFIGURE_SIGNAL),
                                      "reload",
                /* reload实际上是执行reload的nginx进程向原master+worker中的master进程发送reload信号,源master收到后,启动新的worker进程,同时向源worker
                    进程发送quit信号,等他们处理完已有的数据信息后,退出,这样就只有新的worker进程运行.*/
                                          ngx_signal_handler},

        {ngx_signal_value(NGX_REOPEN_SIGNAL),
                  "SIG" ngx_value(NGX_REOPEN_SIGNAL),
                                      "reopen",
                                          ngx_signal_handler},

        {ngx_signal_value(NGX_NOACCEPT_SIGNAL),
                  "SIG" ngx_value(NGX_NOACCEPT_SIGNAL),
                                      "",
                                          ngx_signal_handler},

        {ngx_signal_value(NGX_TERMINATE_SIGNAL),
                  "SIG" ngx_value(NGX_TERMINATE_SIGNAL),
                                      "stop",
                                          ngx_signal_handler},

        {ngx_signal_value(NGX_SHUTDOWN_SIGNAL),
                  "SIG" ngx_value(NGX_SHUTDOWN_SIGNAL),
                                      "quit",
                                          ngx_signal_handler},

        {ngx_signal_value(NGX_CHANGEBIN_SIGNAL),
                  "SIG" ngx_value(NGX_CHANGEBIN_SIGNAL),
                                      "",
                                          ngx_signal_handler},

        {SIGALRM, "SIGALRM",          "", ngx_signal_handler},

        {SIGINT,  "SIGINT",           "", ngx_signal_handler},

        {SIGIO,   "SIGIO",            "", ngx_signal_handler},

        {SIGCHLD, "SIGCHLD",          "", ngx_signal_handler},

        {SIGSYS,  "SIGSYS, SIG_IGN",  "", NULL},

        {SIGPIPE, "SIGPIPE, SIG_IGN", "", NULL},

        {0, NULL,                     "", NULL}
};

/*master进程怎样启动一个子进程呢?其实很简单,fork系统调用即可以完成.ngx_spawn_process方法封装了fork系统调用,
并且会从ngx_processes数组中选择一个还未使用的ngx_process_t元素存储这个子进程的相关信息.如果所有1024个数纽元素中已经没有空
余的元素,也就是说,子进程个数超过了最大值1024,那么将会返回NGX_INVALID_PID.
 因此,ngx_processes数组中元素的初始化将在ngx_spawn_process方法中进行.*/

//第一个参数是全局的配置,第二个参数是子进程需要执行的函数,第三个参数是proc的参数.第四个类型.  name是子进程的名称
ngx_pid_t
ngx_spawn_process(ngx_cycle_t *cycle, ngx_spawn_proc_pt proc, void *data,
                  char *name, ngx_int_t respawn) { //respawn取值为NGX_PROCESS_RESPAWN等,或者为进程在ngx_processes[]中的序号
    u_long on;
    ngx_pid_t pid;
    ngx_int_t s;  //将要创建的子进程在进程表中的位置
    // 如果respawn不小于0,则视为当前进程已经退出,需要重启
    if (respawn >= 0) { //替换进程ngx_processes[respawn],可安全重用该进程表项
        s = respawn;

    } else {
        for (s = 0; s < ngx_last_process; s++) {
            if (ngx_processes[s].pid == -1) { //先找到一个被回收的进程表象
                break;
            }
        }

        if (s == NGX_MAX_PROCESSES) { //最多只能创建1024个子进程
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                          "no more than %d processes can be spawned",
                          NGX_MAX_PROCESSES);
            return NGX_INVALID_PID;
        }
    }


    if (respawn != NGX_PROCESS_DETACHED) {  //不是分离的子进程      /* 不是热代码替换 */

        /* Solaris 9 still has no AF_LOCAL */

        /*这里相当于Master进程调用socketpair()为新的worker进程创建一对全双工的socket

          实际上socketpair 函数跟pipe 函数是类似的,也只能在同个主机上具有亲缘关系的进程间通信,但pipe 创建的匿名管道是半双工的,
          而socketpair 可以认为是创建一个全双工的管道.
          int socketpair(int domain, int type, int protocol, int sv[2]);
          这个方法可以创建一对关联的套接字sv[2].下面依次介绍它的4个参数:参数d表示域,在Linux下通常取值为AF UNIX;type取值为SOCK.
          STREAM或者SOCK.DGRAM,它表示在套接字上使用的是TCP还是UDP; protocol必须传递0;sv[2]是一个含有两个元素的整型数组,实际上就
          是两个套接字.当socketpair返回0时,sv[2]这两个套接字创建成功,否则socketpair返回一1表示失败.
             当socketpair执行成功时,sv[2]这两个套接字具备下列关系:向sv[0]套接字写入数据,将可以从sv[l]套接字中读取到刚写入的数据;
          同样,向sv[l]套接字写入数据,也可以从sv[0]中读取到写入的数据.通常,在父、子进程通信前,会先调用socketpair方法创建这样一组
          套接字,在调用fork方法创建出子进程后,将会在父进程中关闭sv[l]套接字,仅使用sv[0]套接字用于向子进程发送数据以及接收子进程发
          送来的数据:而在子进程中则关闭sv[0]套接字,仅使用sv[l]套接字既可以接收父进程发来的数据,也可以向父进程发送数据.
          注意socketpair的协议族为AF_UNIX UNXI域*/
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, ngx_processes[s].channel) == -1) { //在ngx_worker_process_init中添加到事件集
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "socketpair() failed while spawning \"%s\"", name);
            return NGX_INVALID_PID;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                       "channel %d:%d",
                       ngx_processes[s].channel[0],
                       ngx_processes[s].channel[1]);
        /* 设置master的channel[0](即写端口),channel[1](即读端口)均为非阻塞方式 */
        if (ngx_nonblocking(ngx_processes[s].channel[0]) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          ngx_nonblocking_n " failed while spawning \"%s\"",
                          name);
            ngx_close_channel(ngx_processes[s].channel, cycle->log);
            return NGX_INVALID_PID;
        }

        if (ngx_nonblocking(ngx_processes[s].channel[1]) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          ngx_nonblocking_n " failed while spawning \"%s\"",
                          name);
            ngx_close_channel(ngx_processes[s].channel, cycle->log);
            return NGX_INVALID_PID;
        }
        /*设置异步模式: 这里可以看下《网络编程卷一》的ioctl函数和fcntl函数 or 网上查询*/
        on = 1; // 标记位,ioctl用于清除(0)或设置(非0)操作
        /*设置channel[0]的信号驱动异步I/O标志
         FIOASYNC:该状态标志决定是否收取针对socket的异步I/O信号(SIGIO)
         其与O_ASYNC文件状态标志等效,可通过fcntl的F_SETFL命令设置or清除*/
        if (ioctl(ngx_processes[s].channel[0], FIOASYNC, &on) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "ioctl(FIOASYNC) failed while spawning \"%s\"", name);
            ngx_close_channel(ngx_processes[s].channel, cycle->log);
            return NGX_INVALID_PID;
        }
        /* F_SETOWN:用于指定接收SIGIO和SIGURG信号的socket属主(进程ID或进程组ID)
         * 这里意思是指定Master进程接收SIGIO和SIGURG信号
         * SIGIO信号必须是在socket设置为信号驱动异步I/O才能产生,即上一步操作
         * SIGURG信号是在新的带外数据到达socket时产生的*/
        if (fcntl(ngx_processes[s].channel[0], F_SETOWN, ngx_pid) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "fcntl(F_SETOWN) failed while spawning \"%s\"", name);
            ngx_close_channel(ngx_processes[s].channel, cycle->log);
            return NGX_INVALID_PID;
        }
        /* FD_CLOEXEC:用来设置文件的close-on-exec状态标准
         *             在exec()调用后,close-on-exec标志为0的情况下,此文件不被关闭;非零则在exec()后被关闭
         *             默认close-on-exec状态为0,需要通过FD_CLOEXEC设置
         *             这里意思是当Master父进程执行了exec()调用后,关闭socket
         */
        if (fcntl(ngx_processes[s].channel[0], F_SETFD, FD_CLOEXEC) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "fcntl(FD_CLOEXEC) failed while spawning \"%s\"",
                          name);
            ngx_close_channel(ngx_processes[s].channel, cycle->log);
            return NGX_INVALID_PID;
        }
        /* 同上,这里意思是当Worker子进程执行了exec()调用后,关闭socket */
        if (fcntl(ngx_processes[s].channel[1], F_SETFD, FD_CLOEXEC) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "fcntl(FD_CLOEXEC) failed while spawning \"%s\"",
                          name);
            ngx_close_channel(ngx_processes[s].channel, cycle->log);
            return NGX_INVALID_PID;
        }
        /*设置即将创建子进程的channel ,这个在后面会用到,在后面创建的子进程的cycle循环执行函数中会用到,例如ngx_worker_process_init -> ngx_add_channel_event
         从而把子进程的channel[1]读端添加到epool中,用于读取父进程发送的ngx_channel_t信息*/
        ngx_channel = ngx_processes[s].channel[1];

    } else {
        ngx_processes[s].channel[0] = -1;
        ngx_processes[s].channel[1] = -1;
    }

    ngx_process_slot = s; // 这一步将在ngx_pass_open_channel()中用到,就是设置下标,用于寻找本次创建的子进程


    pid = fork();

    switch (pid) {

        case -1:
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "fork() failed while spawning \"%s\"", name);
            ngx_close_channel(ngx_processes[s].channel, cycle->log);
            return NGX_INVALID_PID;

        case 0: //子进程
            ngx_parent = ngx_pid; // 设置子进程ID
            ngx_pid = ngx_getpid();
            //printf(" .....slave......pid:%u, %u\n", pid, ngx_pid); slave......pid:0, 14127
            proc(cycle, data); // 调用proc回调函数,即ngx_worker_process_cycle.之后worker子进程从这里开始执行
            break;

        default://父进程,但是这时候打印的pid为子进程ID
            //printf(" ......master.....pid:%u, %u\n", pid, ngx_pid); master.....pid:14127, 14126
            break;
    }

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "start %s %P", name, pid);
    /* 这一部分用来设置ngx_process_t的成员变量 */
    ngx_processes[s].pid = pid;
    ngx_processes[s].exited = 0;

    if (respawn >= 0) { /* 如果大于0,则说明是在重启子进程,因此下面的初始化不用再重复做 */
        return pid;
    }

    ngx_processes[s].proc = proc;
    ngx_processes[s].data = data;
    ngx_processes[s].name = name;
    ngx_processes[s].exiting = 0;

    switch (respawn) { /* OK,也不多说了,用来设置状态信息 */

        case NGX_PROCESS_NORESPAWN:
            ngx_processes[s].respawn = 0;
            ngx_processes[s].just_spawn = 0;
            ngx_processes[s].detached = 0;
            break;

        case NGX_PROCESS_JUST_SPAWN:
            ngx_processes[s].respawn = 0;
            ngx_processes[s].just_spawn = 1;
            ngx_processes[s].detached = 0;
            break;

        case NGX_PROCESS_RESPAWN:
            ngx_processes[s].respawn = 1;
            ngx_processes[s].just_spawn = 0;
            ngx_processes[s].detached = 0;
            break;

        case NGX_PROCESS_JUST_RESPAWN:
            ngx_processes[s].respawn = 1;
            ngx_processes[s].just_spawn = 1;
            ngx_processes[s].detached = 0;
            break;

        case NGX_PROCESS_DETACHED:
            ngx_processes[s].respawn = 0;
            ngx_processes[s].just_spawn = 0;
            ngx_processes[s].detached = 1;
            break;
    }

    if (s == ngx_last_process) {
        ngx_last_process++;
    }

    return pid;
}


ngx_pid_t
ngx_execute(ngx_cycle_t *cycle, ngx_exec_ctx_t *ctx) {
    return ngx_spawn_process(cycle, ngx_execute_proc, ctx, ctx->name,
                             NGX_PROCESS_DETACHED);
}


static void
ngx_execute_proc(ngx_cycle_t *cycle, void *data) {
    ngx_exec_ctx_t *ctx = data;
    /*execve()用来执行参数filename字符串所代表的文件路径,第二个参数是利用指针数组来传递给执行文件,并且需
        要以空指针(NULL)结束,最后一个参数则为传递给执行文件的新环境变量数组*/
    if (execve(ctx->path, ctx->argv, ctx->envp) == -1) { //把旧master进程bind监听的fd写入到环境变量NGINX_VAR中
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "execve() failed while executing %s \"%s\"",
                      ctx->name, ctx->path);
    }

    exit(1);
}


//信号处理在ngx_signal_handler
ngx_int_t
ngx_init_signals(ngx_log_t *log) {
    ngx_signal_t *sig;
    struct sigaction sa;

    for (sig = signals; sig->signo != 0; sig++) {
        ngx_memzero(&sa, sizeof(struct sigaction));

        if (sig->handler) {
            sa.sa_sigaction = sig->handler;
            sa.sa_flags = SA_SIGINFO;

        } else {
            sa.sa_handler = SIG_IGN;
        }

        sigemptyset(&sa.sa_mask);
        if (sigaction(sig->signo, &sa, NULL) == -1) {
#if (NGX_VALGRIND)
            ngx_log_error(NGX_LOG_ALERT, log, ngx_errno,
                          "sigaction(%s) failed, ignored", sig->signame);
#else
            ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                          "sigaction(%s) failed", sig->signame);
            return NGX_ERROR;
#endif
        }
    }

    return NGX_OK;
}


//注册新号在ngx_init_signals
static void
ngx_signal_handler(int signo, siginfo_t *siginfo, void *ucontext) {
    char *action;
    ngx_int_t ignore;
    ngx_err_t err;
    ngx_signal_t *sig;

    ignore = 0;

    err = ngx_errno;

    for (sig = signals; sig->signo != 0; sig++) {
        if (sig->signo == signo) {
            break;
        }
    }

    ngx_time_sigsafe_update();

    action = "";

    switch (ngx_process) {

        case NGX_PROCESS_MASTER:
        case NGX_PROCESS_SINGLE:
            switch (signo) {
                //当接收到QUIT信号时,ngx_quit标志位会设为1,这是在告诉worker进程需要优雅地关闭进程
                case ngx_signal_value(NGX_SHUTDOWN_SIGNAL):
                    ngx_quit = 1;
                    action = ", shutting down";
                    break;
                    //当接收到TERM信号时,ngx_terminate标志位会设为1,这是在告诉worker进程需要强制关闭进程
                case ngx_signal_value(NGX_TERMINATE_SIGNAL):
                case SIGINT:
                    ngx_terminate = 1;
                    action = ", exiting";
                    break;

                case ngx_signal_value(NGX_NOACCEPT_SIGNAL):
                    if (ngx_daemonized) {
                        ngx_noaccept = 1;
                        action = ", stop accepting connections";
                    }
                    break;

                case ngx_signal_value(NGX_RECONFIGURE_SIGNAL):  //reload信号,是由master处理
                    ngx_reconfigure = 1;
                    action = ", reconfiguring";
                    break;
                    //当接收到USRI信号时,ngx_reopen标志位会设为1,这是在告诉Nginx需要重新打开文件(如切换日志文件时）
                case ngx_signal_value(NGX_REOPEN_SIGNAL):
                    ngx_reopen = 1;
                    action = ", reopening logs";
                    break;

                case ngx_signal_value(NGX_CHANGEBIN_SIGNAL):
                    if (ngx_getppid() == ngx_parent || ngx_new_binary > 0) {
                        //nginx热升级通过发送该信号,这里必须保证父进程大于1,父进程小于等于1的话,说明已经由就master启动了本master,则就不能热升级
                        //所以如果通过crt登录启动nginx的话,可以看到其PPID大于1,所以不能热升级
                        /*
                         * Ignore the signal in the new binary if its parent is
                         * not changed, i.e. the old binary's process is still
                         * running.  Or ignore the signal in the old binary's
                         * process if the new binary's process is already running.
                         */

                        action = ", ignoring";
                        ignore = 1;
                        break;
                    }

                    ngx_change_binary = 1;
                    action = ", changing binary";
                    break;

                case SIGALRM:  //子进程会重新设置定时器信号,见ngx_timer_signal_handler
                    ngx_sigalrm = 1;
                    break;

                case SIGIO:
                    ngx_sigio = 1;
                    break;

                case SIGCHLD: //子进程终止, 这时候内核同时向父进程发送个sigchld信号.等待父进程waitpid回收,避免僵死进程
                    ngx_reap = 1;
                    break;
            }

            break;

        case NGX_PROCESS_WORKER:
        case NGX_PROCESS_HELPER:
            switch (signo) {
                case ngx_signal_value(NGX_NOACCEPT_SIGNAL):
                    if (!ngx_daemonized) {
                        break;
                    }
                    ngx_debug_quit = 1;
                    /* fall through */
                case ngx_signal_value(NGX_SHUTDOWN_SIGNAL): //工作进程收到quit信号
                    ngx_quit = 1;
                    action = ", shutting down";
                    break;
                case ngx_signal_value(NGX_TERMINATE_SIGNAL):
                case SIGINT:
                    ngx_terminate = 1;
                    action = ", exiting";
                    break;

                case ngx_signal_value(NGX_REOPEN_SIGNAL):
                    ngx_reopen = 1;
                    action = ", reopening logs";
                    break;

                case ngx_signal_value(NGX_RECONFIGURE_SIGNAL):
                case ngx_signal_value(NGX_CHANGEBIN_SIGNAL):
                case SIGIO:
                    action = ", ignoring";
                    break;
            }

            break;
    }

    if (siginfo && siginfo->si_pid) {
        ngx_log_error(NGX_LOG_NOTICE, ngx_cycle->log, 0,
                      "signal %d (%s) received from %P%s",
                      signo, sig->signame, siginfo->si_pid, action);

    } else {
        ngx_log_error(NGX_LOG_NOTICE, ngx_cycle->log, 0,
                      "signal %d (%s) received%s",
                      signo, sig->signame, action);
    }

    if (ignore) {
        ngx_log_error(NGX_LOG_CRIT, ngx_cycle->log, 0,
                      "the changing binary signal is ignored: "
                      "you should shutdown or terminate "
                      "before either old or new binary's process");
    }

    if (signo == SIGCHLD) { //回收子进程资源waitpid
        ngx_process_get_status();
    }

    ngx_set_errno(err);
}


static void
ngx_process_get_status(void) {
    int status;
    char *process;
    ngx_pid_t pid;
    ngx_err_t err;
    ngx_int_t i;
    ngx_uint_t one;

    one = 0;

    for (;;) {
        pid = waitpid(-1, &status, WNOHANG);

        if (pid == 0) {
            return;
        }

        if (pid == -1) {
            err = ngx_errno;

            if (err == NGX_EINTR) {
                continue;
            }

            if (err == NGX_ECHILD && one) {
                return;
            }

            /*
             * Solaris always calls the signal handler for each exited process
             * despite waitpid() may be already called for this process.
             *
             * When several processes exit at the same time FreeBSD may
             * erroneously call the signal handler for exited process
             * despite waitpid() may be already called for this process.
             */

            if (err == NGX_ECHILD) {
                ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, err,
                              "waitpid() failed");
                return;
            }

            ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, err,
                          "waitpid() failed");
            return;
        }


        one = 1;
        process = "unknown process";

        for (i = 0; i < ngx_last_process; i++) {
            if (ngx_processes[i].pid == pid) {
                ngx_processes[i].status = status;
                ngx_processes[i].exited = 1;
                process = ngx_processes[i].name;
                break;
            }
        }

        if (WTERMSIG(status)) {
#ifdef WCOREDUMP
            ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
                          "%s %P exited on signal %d%s",
                          process, pid, WTERMSIG(status),
                          WCOREDUMP(status) ? " (core dumped)" : "");
#else
            ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
                          "%s %P exited on signal %d",
                          process, pid, WTERMSIG(status));
#endif

        } else {
            ngx_log_error(NGX_LOG_NOTICE, ngx_cycle->log, 0,
                          "%s %P exited with code %d",
                          process, pid, WEXITSTATUS(status));
        }

        if (WEXITSTATUS(status) == 2 && ngx_processes[i].respawn) {
            ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
                          "%s %P exited with fatal code %d "
                          "and cannot be respawned",
                          process, pid, WEXITSTATUS(status));
            ngx_processes[i].respawn = 0;
        }

        ngx_unlock_mutexes(pid);
    }
}


static void
ngx_unlock_mutexes(ngx_pid_t pid) {
    ngx_uint_t i;
    ngx_shm_zone_t *shm_zone;
    ngx_list_part_t *part;
    ngx_slab_pool_t *sp;

    /*
     * unlock the accept mutex if the abnormally exited process
     * held it
     */

    if (ngx_accept_mutex_ptr) {
        (void) ngx_shmtx_force_unlock(&ngx_accept_mutex, pid);
    }

    /*
     * unlock shared memory mutexes if held by the abnormally exited
     * process
     */

    part = (ngx_list_part_t *) &ngx_cycle->shared_memory.part;
    shm_zone = part->elts;

    for (i = 0; /* void */ ; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            shm_zone = part->elts;
            i = 0;
        }

        sp = (ngx_slab_pool_t *) shm_zone[i].shm.addr;

        if (ngx_shmtx_force_unlock(&sp->mutex, pid)) {
            ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
                          "shared memory zone \"%V\" was locked by %P",
                          &shm_zone[i].shm.name, pid);
        }
    }
}


void
ngx_debug_point(void) { //让自己停止,通知父进程
    ngx_core_conf_t *ccf;

    ccf = (ngx_core_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx,
                                           ngx_core_module);

    switch (ccf->debug_points) {

        case NGX_DEBUG_POINTS_STOP:
            raise(SIGSTOP);
            break;

        case NGX_DEBUG_POINTS_ABORT:
            ngx_abort();
    }
}

/*遍历signals数组,根据给定信号name,找到对应signo;调用kill向该pid发送signo号信号;*/
ngx_int_t
ngx_os_signal_process(ngx_cycle_t *cycle, char *name, ngx_pid_t pid) {
    ngx_signal_t *sig;

    for (sig = signals; sig->signo != 0; sig++) {
        if (ngx_strcmp(name, sig->name) == 0) {
            if (kill(pid, sig->signo) != -1) { //这里发送signal,信号接收处理在signals->handler
                return 0;
            }

            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "kill(%P, %d) failed", pid, sig->signo);
        }
    }

    return 1;
}
