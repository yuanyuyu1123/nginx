
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_LOG_H_INCLUDED_
#define _NGX_LOG_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


//stderr (0)>= emerg(1) >= alert(2) >= crit(3) >= err(4)>= warn(5) >= notice(6) >= info(7) >= debug(8)
//debug级别最低,stderr级别最高;圆括号中的数据是对应日志等级的值.
//log->log_level中的低4位取值为NGX_LOG_STDERR等  5-12位取值为位图,表示对应模块的日志   另外NGX_LOG_DEBUG_CONNECTION NGX_LOG_DEBUG_ALL对应connect日志和所有日志
//下面这些通过ngx_log_error输出  对应err_levels[]参考ngx_log_set_levels
#define NGX_LOG_STDERR            0
#define NGX_LOG_EMERG             1
#define NGX_LOG_ALERT             2
#define NGX_LOG_CRIT              3
#define NGX_LOG_ERR               4
#define NGX_LOG_WARN              5 //如果level > NGX_LOG_WARN则不会在屏幕前台打印,见ngx_log_error_core
#define NGX_LOG_NOTICE            6
#define NGX_LOG_INFO              7
#define NGX_LOG_DEBUG             8

//下面这些是通过与操作判断是否需要打印,可以参考ngx_log_debug7
//log->log_level中的低4位取值为NGX_LOG_STDERR等  5-12位取值为位图,表示对应模块的日志
//另外NGX_LOG_DEBUG_CONNECTION NGX_LOG_DEBUG_ALL对应connect日志和所有日志
//如果通过加参数debug_http则会打开NGX_LOG_DEBUG_HTTP开关,见debug_levels  ngx_log_set_levels,
//如果打开下面开关中的一个,则NGX_LOG_STDERR到NGX_LOG_DEBUG会全部打开,因为log_level很大
//下面这些通过ngx_log_debug0 -- ngx_log_debug8输出  对应debug_levels[] 参考ngx_log_set_levels
#define NGX_LOG_DEBUG_CORE        0x010
#define NGX_LOG_DEBUG_ALLOC       0x020
#define NGX_LOG_DEBUG_MUTEX       0x040
#define NGX_LOG_DEBUG_EVENT       0x080
#define NGX_LOG_DEBUG_HTTP        0x100
#define NGX_LOG_DEBUG_MAIL        0x200
#define NGX_LOG_DEBUG_STREAM      0x400

/*
 * do not forget to update debug_levels[] in src/core/ngx_log.c
 * after the adding a new debug level
 */
//log->log_level中的低4位取值为NGX_LOG_STDERR等  5-12位取值为位图,表示对应模块的日志   另外NGX_LOG_DEBUG_CONNECTION NGX_LOG_DEBUG_ALL对应connect日志和所有日志
#define NGX_LOG_DEBUG_FIRST       NGX_LOG_DEBUG_CORE
#define NGX_LOG_DEBUG_LAST        NGX_LOG_DEBUG_STREAM
#define NGX_LOG_DEBUG_CONNECTION  0x80000000 // --with-debug) NGX_DEBUG=YES  会打开连接日志
#define NGX_LOG_DEBUG_ALL         0x7ffffff0


typedef u_char *(*ngx_log_handler_pt)(ngx_log_t *log, u_char *buf, size_t len);

typedef void (*ngx_log_writer_pt)(ngx_log_t *log, ngx_uint_t level,
                                  u_char *buf, size_t len);


struct ngx_log_s {
    //如果设置的log级别为debug,则会在ngx_log_set_levels把level设置为NGX_LOG_DEBUG_ALL
    //赋值见ngx_log_set_levels
    ngx_uint_t log_level; //日志级别或者日志类型  默认为NGX_LOG_ERR  如果通过error_log  logs/error.log  info;则为设置的等级  比该级别下的日志可以打印
    ngx_open_file_t *file; //日志文件

    ngx_atomic_uint_t connection; //连接数,不为O时会输出到日志中

    time_t disk_full_time;

    /* 记录日志时的回调方法.当handler已经实现(不为NULL),并且不是DEBUG调试级别时,才会调用handler钩子方法 */
    ngx_log_handler_pt   handler; //从连接池获取ngx_connection_t后,c->log->handler = ngx_http_log_error;

    /*
    每个模块都可以自定义data的使用方法.通常,data参数都是在实现了上面的handler回调方法后
    才使用的.例如,HTTP框架就定义了handler方法,并在data中放入了这个请求的上下文信息,这样每次输出日
    志时都会把这个请求URI输出到日志的尾部
    */
    void                *data; //指向ngx_http_log_ctx_t,见ngx_http_init_connection

    ngx_log_writer_pt writer;
    void *wdata;

    /*
     * we declare "action" as "char *" because the actions are usually
     * the static strings and in the "u_char *" case we have to override
     * their types all the time
     */
    /* 表示当前的动作.实际上,action与data是一样的,只有在实现了handler回调方法后才会使用.
     * 例如,HTTP框架就在handler方法中检查action是否为NULL,如果不为NULL,就会在日志后加入"while"+action,
     * 以此表示当前日志是在进行什么操作,帮助定位问题*/
    char *action;
    //ngx_log_insert插入,在ngx_log_error_core找到对应级别的日志配置进行输出,因为可以配置error_log不同级别的日志存储在不同的日志文件中
    ngx_log_t *next;
};


#define NGX_MAX_ERROR_STR   2048


/*********************************/

#if (NGX_HAVE_C99_VARIADIC_MACROS)

#define NGX_HAVE_VARIADIC_MACROS  1

#define ngx_log_error(level, log, ...)                                        \
    if ((log)->log_level >= level) ngx_log_error_core(level, log, __VA_ARGS__)

void ngx_log_error_core(ngx_uint_t level, ngx_log_t *log, ngx_err_t err,
                        const char *fmt, ...);

#define ngx_log_debug(level, log, ...)                                        \
    if ((log)->log_level & level)                                             \
        ngx_log_error_core(NGX_LOG_DEBUG, log, __VA_ARGS__)

/*********************************/

#elif (NGX_HAVE_GCC_VARIADIC_MACROS)

#define NGX_HAVE_VARIADIC_MACROS  1

#define ngx_log_error(level, log, args...)                                    \
    if ((log)->log_level >= level) ngx_log_error_core(level, log, args)

void ngx_log_error_core(ngx_uint_t level, ngx_log_t *log, ngx_err_t err,
    const char *fmt, ...);

#define ngx_log_debug(level, log, args...)                                    \
    if ((log)->log_level & level)                                             \
        ngx_log_error_core(NGX_LOG_DEBUG, log, args)

/*********************************/

#else /* no variadic macros */

#define NGX_HAVE_VARIADIC_MACROS  0

void ngx_cdecl ngx_log_error(ngx_uint_t level, ngx_log_t *log, ngx_err_t err,
    const char *fmt, ...);
void ngx_log_error_core(ngx_uint_t level, ngx_log_t *log, ngx_err_t err,
    const char *fmt, va_list args);
void ngx_cdecl ngx_log_debug_core(ngx_log_t *log, ngx_err_t err,
    const char *fmt, ...);


#endif /* variadic macros */


/*********************************/

#if (NGX_DEBUG)

#if (NGX_HAVE_VARIADIC_MACROS)

#define ngx_log_debug0(level, log, err, fmt)                                  \
        ngx_log_debug(level, log, err, fmt)

#define ngx_log_debug1(level, log, err, fmt, arg1)                            \
        ngx_log_debug(level, log, err, fmt, arg1)

#define ngx_log_debug2(level, log, err, fmt, arg1, arg2)                      \
        ngx_log_debug(level, log, err, fmt, arg1, arg2)

#define ngx_log_debug3(level, log, err, fmt, arg1, arg2, arg3)                \
        ngx_log_debug(level, log, err, fmt, arg1, arg2, arg3)

#define ngx_log_debug4(level, log, err, fmt, arg1, arg2, arg3, arg4)          \
        ngx_log_debug(level, log, err, fmt, arg1, arg2, arg3, arg4)

#define ngx_log_debug5(level, log, err, fmt, arg1, arg2, arg3, arg4, arg5)    \
        ngx_log_debug(level, log, err, fmt, arg1, arg2, arg3, arg4, arg5)

#define ngx_log_debug6(level, log, err, fmt,                                  \
                       arg1, arg2, arg3, arg4, arg5, arg6)                    \
        ngx_log_debug(level, log, err, fmt,                                   \
                       arg1, arg2, arg3, arg4, arg5, arg6)

#define ngx_log_debug7(level, log, err, fmt,                                  \
                       arg1, arg2, arg3, arg4, arg5, arg6, arg7)              \
        ngx_log_debug(level, log, err, fmt,                                   \
                       arg1, arg2, arg3, arg4, arg5, arg6, arg7)

#define ngx_log_debug8(level, log, err, fmt,                                  \
                       arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8)        \
        ngx_log_debug(level, log, err, fmt,                                   \
                       arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8)


#else /* no variadic macros */

#define ngx_log_debug0(level, log, err, fmt)                                  \
    if ((log)->log_level & level)                                             \
        ngx_log_debug_core(log, err, fmt)

#define ngx_log_debug1(level, log, err, fmt, arg1)                            \
    if ((log)->log_level & level)                                             \
        ngx_log_debug_core(log, err, fmt, arg1)

#define ngx_log_debug2(level, log, err, fmt, arg1, arg2)                      \
    if ((log)->log_level & level)                                             \
        ngx_log_debug_core(log, err, fmt, arg1, arg2)

#define ngx_log_debug3(level, log, err, fmt, arg1, arg2, arg3)                \
    if ((log)->log_level & level)                                             \
        ngx_log_debug_core(log, err, fmt, arg1, arg2, arg3)

#define ngx_log_debug4(level, log, err, fmt, arg1, arg2, arg3, arg4)          \
    if ((log)->log_level & level)                                             \
        ngx_log_debug_core(log, err, fmt, arg1, arg2, arg3, arg4)

#define ngx_log_debug5(level, log, err, fmt, arg1, arg2, arg3, arg4, arg5)    \
    if ((log)->log_level & level)                                             \
        ngx_log_debug_core(log, err, fmt, arg1, arg2, arg3, arg4, arg5)

#define ngx_log_debug6(level, log, err, fmt,                                  \
                       arg1, arg2, arg3, arg4, arg5, arg6)                    \
    if ((log)->log_level & level)                                             \
        ngx_log_debug_core(log, err, fmt, arg1, arg2, arg3, arg4, arg5, arg6)

#define ngx_log_debug7(level, log, err, fmt,                                  \
                       arg1, arg2, arg3, arg4, arg5, arg6, arg7)              \
    if ((log)->log_level & level)                                             \
        ngx_log_debug_core(log, err, fmt,                                     \
                       arg1, arg2, arg3, arg4, arg5, arg6, arg7)

#define ngx_log_debug8(level, log, err, fmt,                                  \
                       arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8)        \
    if ((log)->log_level & level)                                             \
        ngx_log_debug_core(log, err, fmt,                                     \
                       arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8)

#endif

#else /* !NGX_DEBUG */
/* 注意不能用%V输出,否则会出现段错误 */
#define ngx_log_debug0(level, log, err, fmt)
#define ngx_log_debug1(level, log, err, fmt, arg1)
#define ngx_log_debug2(level, log, err, fmt, arg1, arg2)
#define ngx_log_debug3(level, log, err, fmt, arg1, arg2, arg3)
#define ngx_log_debug4(level, log, err, fmt, arg1, arg2, arg3, arg4)
#define ngx_log_debug5(level, log, err, fmt, arg1, arg2, arg3, arg4, arg5)
#define ngx_log_debug6(level, log, err, fmt, arg1, arg2, arg3, arg4, arg5, arg6)
#define ngx_log_debug7(level, log, err, fmt, arg1, arg2, arg3, arg4, arg5, \
                       arg6, arg7)
#define ngx_log_debug8(level, log, err, fmt, arg1, arg2, arg3, arg4, arg5, \
                       arg6, arg7, arg8)

#endif

/*********************************/

ngx_log_t *ngx_log_init(u_char *prefix, u_char *error_log);

void ngx_cdecl ngx_log_abort(ngx_err_t err, const char *fmt, ...);

void ngx_cdecl ngx_log_stderr(ngx_err_t err, const char *fmt, ...);

u_char *ngx_log_errno(u_char *buf, u_char *last, ngx_err_t err);

ngx_int_t ngx_log_open_default(ngx_cycle_t *cycle);

ngx_int_t ngx_log_redirect_stderr(ngx_cycle_t *cycle);

ngx_log_t *ngx_log_get_file_log(ngx_log_t *head);

char *ngx_log_set_log(ngx_conf_t *cf, ngx_log_t **head);


/*
 * ngx_write_stderr() cannot be implemented as macro, since
 * MSVC does not allow to use #ifdef inside macro parameters.
 *
 * ngx_write_fd() is used instead of ngx_write_console(), since
 * CharToOemBuff() inside ngx_write_console() cannot be used with
 * read only buffer as destination and CharToOemBuff() is not needed
 * for ngx_write_stderr() anyway.
 */
static ngx_inline void
ngx_write_stderr(char *text) {
    (void) ngx_write_fd(ngx_stderr, text, ngx_strlen(text));
}


static ngx_inline void
ngx_write_stdout(char *text) {
    (void) ngx_write_fd(ngx_stdout, text, ngx_strlen(text));
}


extern ngx_module_t ngx_errlog_module;
extern ngx_uint_t ngx_use_stderr;


#endif /* _NGX_LOG_H_INCLUDED_ */
