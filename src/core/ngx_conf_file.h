
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_CONF_FILE_H_INCLUDED_
#define _NGX_CONF_FILE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/*
 *        AAAA  number of arguments
 *      FF      command flags
 *    TT        command type, i.e. HTTP "location" or "server" command
 */

/*
以下这些宏用于限制配置项的参数个数
NGX_CONF_NOARGS：配置项不允许带参数
NGX_CONF_TAKE1：配置项可以带1个参数
NGX_CONF_TAKE2：配置项可以带2个参数
NGX_CONF_TAKE3：配置项可以带3个参数
NGX_CONF_TAKE4：配置项可以带4个参数
NGX_CONF_TAKE5：配置项可以带5个参数
NGX_CONF_TAKE6：配置项可以带6个参数
NGX_CONF_TAKE7：配置项可以带7个参数
NGX_CONF_TAKE12：配置项可以带1或2个参数
NGX_CONF_TAKE13：配置项可以带1或3个参数
NGX_CONF_TAKE23：配置项可以带2或3个参数
NGX_CONF_TAKE123：配置项可以带1-3个参数
NGX_CONF_TAKE1234：配置项可以带1-4个参数
*/
#define NGX_CONF_NOARGS      0x00000001
#define NGX_CONF_TAKE1       0x00000002
#define NGX_CONF_TAKE2       0x00000004
#define NGX_CONF_TAKE3       0x00000008
#define NGX_CONF_TAKE4       0x00000010
#define NGX_CONF_TAKE5       0x00000020
#define NGX_CONF_TAKE6       0x00000040
#define NGX_CONF_TAKE7       0x00000080

#define NGX_CONF_MAX_ARGS    8

#define NGX_CONF_TAKE12      (NGX_CONF_TAKE1|NGX_CONF_TAKE2)
#define NGX_CONF_TAKE13      (NGX_CONF_TAKE1|NGX_CONF_TAKE3)

#define NGX_CONF_TAKE23      (NGX_CONF_TAKE2|NGX_CONF_TAKE3)

#define NGX_CONF_TAKE123     (NGX_CONF_TAKE1|NGX_CONF_TAKE2|NGX_CONF_TAKE3)
#define NGX_CONF_TAKE1234    (NGX_CONF_TAKE1|NGX_CONF_TAKE2|NGX_CONF_TAKE3   \
                              |NGX_CONF_TAKE4)

#define NGX_CONF_ARGS_NUMBER 0x000000ff

/*
以下这些宏用于限制配置项参数形式
NGX_CONF_BLOCK：配置项定义了一种新的{}块，如：http、server等配置项。
NGX_CONF_ANY：不验证配置项携带的参数个数。
NGX_CONF_FLAG：配置项只能带一个参数，并且参数必需是on或者off。
NGX_CONF_1MORE：配置项携带的参数必需超过一个。
NGX_CONF_2MORE：配置项携带的参数必需超过二个。
*/
#define NGX_CONF_BLOCK       0x00000100
#define NGX_CONF_FLAG        0x00000200
#define NGX_CONF_ANY         0x00000400
#define NGX_CONF_1MORE       0x00000800
#define NGX_CONF_2MORE       0x00001000

//使用全局配置，主要包括以下命令
// ngx_core_commands  ngx_openssl_commands  ngx_google_perftools_commands
// ngx_regex_commands  ngx_thread_pool_commands
#define NGX_DIRECT_CONF      0x00010000 //都是和NGX_MAIN_CONF一起使用

/*
NGX_MAIN_CONF：配置项可以出现在全局配置中，即不属于任何{}配置块。
NGX_EVENT_CONF：配置项可以出现在events{}块内。
NGX_HTTP_MAIN_CONF： 配置项可以出现在http{}块内。
NGX_HTTP_SRV_CONF:：配置项可以出现在server{}块内，该server块必需属于http{}块。
NGX_HTTP_LOC_CONF：配置项可以出现在location{}块内，该location块必需属于server{}块。
NGX_HTTP_UPS_CONF： 配置项可以出现在upstream{}块内，该location块必需属于http{}块。
NGX_HTTP_SIF_CONF：配置项可以出现在server{}块内的if{}块中。该if块必须属于http{}块。
NGX_HTTP_LIF_CONF: 配置项可以出现在location{}块内的if{}块中。该if块必须属于http{}块。
NGX_HTTP_LMT_CONF: 配置项可以出现在limit_except{}块内,该limit_except块必须属于http{}块。
*/

//支持NGX_MAIN_CONF | NGX_DIRECT_CONF的包括:
//ngx_core_commands  ngx_openssl_commands  ngx_google_perftools_commands   ngx_regex_commands  ngx_thread_pool_commands
//这些一般是一级配置里面的配置项，http{}外的

/*总结，一般一级配置(http{}外的配置项)一般属性包括NGX_MAIN_CONF|NGX_DIRECT_CONF。http events等这一行的配置属性,全局配置项worker_priority等也属于这个行列
包括NGX_MAIN_CONF不包括NGX_DIRECT_CONF*/
#define NGX_MAIN_CONF        0x01000000

/*配置类型模块是唯一一种只有1个模块的模块类型。配置模块的类型叫做NGX_CONF_MODULE，它仅有的模块叫做ngx_conf_module，这是Nginx最
底层的模块，它指导着所有模块以配置项为核心来提供功能。因此，它是其他所有模块的基础。*/

//目前未使用，设置与否均无意义
#define NGX_ANY_CONF         0xFF000000


#define NGX_CONF_UNSET       -1
#define NGX_CONF_UNSET_UINT  (ngx_uint_t) -1
#define NGX_CONF_UNSET_PTR   (void *) -1
#define NGX_CONF_UNSET_SIZE  (size_t) -1
#define NGX_CONF_UNSET_MSEC  (ngx_msec_t) -1


#define NGX_CONF_OK          NULL
#define NGX_CONF_ERROR       (void *) -1

#define NGX_CONF_BLOCK_START 1
#define NGX_CONF_BLOCK_DONE  2
#define NGX_CONF_FILE_DONE   3

//GX_CORE_MODULE类型的核心模块解析配置项时，配置项一定是全局的，
/*NGX_CORE_MODULE主要包括以下模块:
ngx_core_module  ngx_events_module  ngx_http_module  ngx_errlog_module  ngx_mail_module
ngx_regex_module  ngx_stream_module  ngx_thread_pool_module*/

//所有的核心模块NGX_CORE_MODULE对应的上下文ctx为ngx_core_module_t，子模块，例如http{} NGX_HTTP_MODULE模块对应的为上下文为ngx_http_module_t
//events{} NGX_EVENT_MODULE模块对应的为上下文为ngx_event_module_t
/*Nginx还定义了一种基础类型的模块：核心模块，它的模块类型叫做NGX_CORE_MODULE。目前官方的核心类型模块中共有6个具体模块，分别
是ngx_core_module、ngx_errlog_module、ngx_events_module、ngx_openssl_module、ngx_http_module、ngx_mail_module模块*/
#define NGX_CORE_MODULE      0x45524F43  /* "CORE" */ //二级模块类型http模块个数，见ngx_http_block  ngx_max_module为NGX_CORE_MODULE(一级模块类型)类型的模块数
//NGX_CONF_MODULE只包括:ngx_conf_module
#define NGX_CONF_MODULE      0x464E4F43  /* "CONF" */


#define NGX_MAX_CONF_ERRSTR  1024


/*https://tengine.taobao.org/book/chapter_3.html*/
struct ngx_command_s { //所有配置的最初源头在ngx_init_cycle
    ngx_str_t name; //配置项名称，如"gzip"
    ngx_uint_t type;
    char *(*set)(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
    // https://blog.csdn.net/qq_62309585/article/details/128619575
    /*可能的值为 NGX_HTTP_MAIN_CONF_OFFSET、NGX_HTTP_SRV_CONF_OFFSET或NGX_HTTP_LOC_CONF_OFFSET.当然也可以直接置为0,就是NGX_HTTP_MAIN_CONF_OFFSET*/
    ngx_uint_t conf;
    ngx_uint_t offset;
    void *post;
};

//ngx_null_command只是一个空的ngx_command_t，表示模块的命令数组解析完毕，如下所示：
#define ngx_null_command  { ngx_null_string, 0, NULL, 0, 0, NULL }


struct ngx_open_file_s {
    ngx_fd_t fd;
    ngx_str_t name;
    void (*flush)(ngx_open_file_t *file, ngx_log_t *log);
    void *data;
};


typedef struct {
    ngx_file_t file; //配置文件名
    ngx_buf_t *buffer; //文件内容在这里面存储
    //当在解析从文件中读取到的4096字节内存时，如果最后面的内存不足以构成一个token，
    //则把这部分内存临时存起来，然后拷贝到下一个4096内存的头部参考ngx_conf_read_token
    ngx_buf_t *dump;
    ngx_uint_t line; //在配置文件中的行号  可以参考ngx_thread_pool_add
} ngx_conf_file_t;


typedef struct {
    ngx_str_t name;
    ngx_buf_t *buffer;
} ngx_conf_dump_t;


typedef char *(*ngx_conf_handler_pt)(ngx_conf_t *cf,
                                     ngx_command_t *dummy, void *conf);

//初始化赋值参考ngx_init_cycle
struct ngx_conf_s {
    char *name;
    ngx_array_t *args; //每解析一行，从配置文件中解析出的配置项全部在这里面
    //最终指向的是一个全局类型的ngx_cycle_s，即ngx_cycle，见ngx_init_cycle
    ngx_cycle_t *cycle; //指向对应的cycle，见ngx_init_cycle中的两行conf.ctx = cycle->conf_ctx; conf.cycle = cycle;
    ngx_pool_t *pool;
    ngx_pool_t *temp_pool; //用该poll的空间都是临时空间，最终在ngx_init_cycle->ngx_destroy_pool(conf.temp_pool);中释放
    ngx_conf_file_t *conf_file; //nginx.conf
    ngx_log_t *log;
    //cycle->conf_ctx = ngx_pcalloc(pool, ngx_max_module * sizeof(void *));
    //指向ngx_cycle_t->conf_ctx 有多少个模块，就有多少个ctx指针数组成员  conf.ctx = cycle->conf_ctx;见ngx_init_cycle
    //这个ctx每次在在进入对应的server{}  location{}前都会指向零时保存父级的ctx，该{}解析完后在恢复到父的ctx。可以参考ngx_http_core_server，ngx_http_core_location
    void *ctx; //指向结构ngx_http_conf_ctx_t  ngx_core_module_t ngx_event_module_t ngx_stream_conf_ctx_t等
    ngx_uint_t module_type; //表示当前配置项是属于那个大类模块 取值有如下5种：NGX_HTTP_MODULE、NGX_CORE_MODULE、NGX_CONF_MODULE、NGX_EVENT_MODULE、NGX_MAIL_MODULE
    ngx_uint_t cmd_type; //大类里面的那个子类模块,如NGX_HTTP_SRV_CONF NGX_HTTP_LOC_CONF等
    ngx_conf_handler_pt handler;
    void *handler_conf;
};


typedef char *(*ngx_conf_post_handler_pt)(ngx_conf_t *cf,
                                          void *data, void *conf);

typedef struct {
    ngx_conf_post_handler_pt post_handler;
} ngx_conf_post_t;


typedef struct {
    ngx_conf_post_handler_pt post_handler;
    char *old_name;
    char *new_name;
} ngx_conf_deprecated_t;


typedef struct {
    ngx_conf_post_handler_pt post_handler;
    ngx_int_t low;
    ngx_int_t high;
} ngx_conf_num_bounds_t;

//表示num号对应的字符串为value,可以参考ngx_conf_set_enum_slot
/*其中，name表示配置项后的参数只能与name指向的字符串相等，而value表示如果参
数中出现了name，ngx_conf_set enum slot方法将会把对应的value设置到存储的变量中。*/
typedef struct { //如ngx_http_core_request_body_in_file
    ngx_str_t name;
    ngx_uint_t value;
} ngx_conf_enum_t;


#define NGX_CONF_BITMASK_SET  1

//可以参考ngx_conf_set_bitmask_slot  test_bitmasks
typedef struct {
    ngx_str_t name;
    ngx_uint_t mask;
} ngx_conf_bitmask_t;


char *ngx_conf_deprecated(ngx_conf_t *cf, void *post, void *data);

char *ngx_conf_check_num_bounds(ngx_conf_t *cf, void *post, void *data);


#define ngx_get_conf(conf_ctx, module)  conf_ctx[module.index]


#define ngx_conf_init_value(conf, default)                                   \
    if (conf == NGX_CONF_UNSET) {                                            \
        conf = default;                                                      \
    }

#define ngx_conf_init_ptr_value(conf, default)                               \
    if (conf == NGX_CONF_UNSET_PTR) {                                        \
        conf = default;                                                      \
    }

#define ngx_conf_init_uint_value(conf, default)                              \
    if (conf == NGX_CONF_UNSET_UINT) {                                       \
        conf = default;                                                      \
    }

#define ngx_conf_init_size_value(conf, default)                              \
    if (conf == NGX_CONF_UNSET_SIZE) {                                       \
        conf = default;                                                      \
    }

#define ngx_conf_init_msec_value(conf, default)                              \
    if (conf == NGX_CONF_UNSET_MSEC) {                                       \
        conf = default;                                                      \
    }

/*事实上，Nginx预设的配置项合并方法有10个，它们的行为与上述的ngx_conf_merge_
str- value是相似的。参见表4-5中Nginx已经实现好的10个简单的配置项合并宏，它们的
参数类型与ngx_conf_merge_str_ value -致，而且除了ngx_conf_merge_bufs value外，它们
都将接收3个参数，分别表示父配置块参数、子配置块参数、默认值。
表4-5  Nginx预设的10种配置项合并宏
┏━━━━━━━━━━━━━┳━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
┃    配置项合并塞          ┃    意义                                                                  ┃
┣━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                          ┃  合并可以使用等号(=)直接赋值的变量，并且该变量在create loc conf等分      ┃
┃ngx_conf_merge_value      ┃配方法中初始化为NGX CONF UNSET，这样类型的成员可以使用ngx_conf_           ┃
┃                          ┃merge_value合并宏                                                         ┃
┣━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                          ┃  合并指针类型的变量，并且该变量在create loc conf等分配方法中初始化为     ┃
┃ngx_conf_merge_ptr_value  ┃NGX CONF UNSET PTR，这样类型的成员可以使用ngx_conf_merge_ptr_value        ┃
┃                          ┃合并宏                                                                    ┃
┣━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                          ┃  合并整数类型的变量，并且该变量在create loc conf等分配方法中初始化为     ┃
┃ngx_conf_merge_uint_value ┃NGX CONF UNSET UINT，这样类型的成员可以使用ngx_conf_merge_uint_           ┃
┃                          ┃ value合并宏                                                              ┃
┣━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                          ┃  合并表示毫秒的ngx_msec_t类型的变量，并且该变量在create loc conf等分     ┃
┃ngx_conf_merge_msec_value ┃配方法中初始化为NGX CONF UNSET MSEC，这样类型的成员可以使用ngx_           ┃
┃                          ┃conf_merge_msec_value合并宏                                               ┃
┣━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                          ┃  舍并表示秒的timej类型的变量，并且该变量在create loc conf等分配方法中    ┃
┃ngx_conf_merge_sec_value  ┃初始化为NGX CONF UNSET，这样类型的成员可以使用ngx_conf_merge_sec_         ┃
┃                          ┃value合并宏                                                               ┃
┣━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                          ┃  合并size-t等表示空间长度的变量，并且该变量在create- loc_ conf等分配方   ┃
┃ngx_conf_merge_size_value ┃法中初始化为NGX。CONF UNSET SIZE，这样类型的成员可以使用ngx_conf_         ┃
┃                          ┃merge_size_value合并宏                                                    ┃
┣━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                          ┃  合并off等表示偏移量的变量，并且该变最在create loc conf等分配方法中      ┃
┃ngx_conf_merge_off_value  ┃初始化为NGX CONF UNSET．这样类型的成员可以使用ngx_conf_merge_off_         ┃
┃                          ┃value合并宏                                                               ┃
┣━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                          ┃  ngx_str_t类型的成员可以使用ngx_conf_merge_str_value合并，这时传人的     ┃
┃ngx_conf_merge_str_value  ┃                                                                          ┃
┃                          ┃default参数必须是一个char水字符串                                         ┃
┣━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                          ┃  ngx_bufs t类型的成员可以使用ngx_conf merge_str_value舍并宏，这时传人的  ┃
┃ngx_conf_merge_bufs_value ┃                                                                          ┃
┃                          ┃default参数是两个，因为ngx_bufsj类型有两个成员，所以需要传人两个默认值    ┃
┣━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃ngx_conf_merge_bitmask_   ┃  以二进制位来表示标志位的整型成员，可以使用ngx_conf_merge_bitmask_       ┃
┃value                     ┃value合并宏                                                               ┃
┗━━━━━━━━━━━━━┻━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
*/
#define ngx_conf_merge_value(conf, prev, default)                            \
    if (conf == NGX_CONF_UNSET) {                                            \
        conf = (prev == NGX_CONF_UNSET) ? default : prev;                    \
    }

#define ngx_conf_merge_ptr_value(conf, prev, default)                        \
    if (conf == NGX_CONF_UNSET_PTR) {                                        \
        conf = (prev == NGX_CONF_UNSET_PTR) ? default : prev;                \
    }

#define ngx_conf_merge_uint_value(conf, prev, default)                       \
    if (conf == NGX_CONF_UNSET_UINT) {                                       \
        conf = (prev == NGX_CONF_UNSET_UINT) ? default : prev;               \
    }

#define ngx_conf_merge_msec_value(conf, prev, default)                       \
    if (conf == NGX_CONF_UNSET_MSEC) {                                       \
        conf = (prev == NGX_CONF_UNSET_MSEC) ? default : prev;               \
    }

#define ngx_conf_merge_sec_value(conf, prev, default)                        \
    if (conf == NGX_CONF_UNSET) {                                            \
        conf = (prev == NGX_CONF_UNSET) ? default : prev;                    \
    }

#define ngx_conf_merge_size_value(conf, prev, default)                       \
    if (conf == NGX_CONF_UNSET_SIZE) {                                       \
        conf = (prev == NGX_CONF_UNSET_SIZE) ? default : prev;               \
    }

#define ngx_conf_merge_off_value(conf, prev, default)                        \
    if (conf == NGX_CONF_UNSET) {                                            \
        conf = (prev == NGX_CONF_UNSET) ? default : prev;                    \
    }

//先判断当前配置项是否已经解析到参数，如果没有，则用父级的参数，如果父级也没解析到该参数则用默认参数
#define ngx_conf_merge_str_value(conf, prev, default)                        \
    if (conf.data == NULL) {                                                 \
        if (prev.data) {                                                     \
            conf.len = prev.len;                                             \
            conf.data = prev.data;                                           \
        } else {                                                             \
            conf.len = sizeof(default) - 1;                                  \
            conf.data = (u_char *) default;                                  \
        }                                                                    \
    }

#define ngx_conf_merge_bufs_value(conf, prev, default_num, default_size)     \
    if (conf.num == 0) {                                                     \
        if (prev.num) {                                                      \
            conf.num = prev.num;                                             \
            conf.size = prev.size;                                           \
        } else {                                                             \
            conf.num = default_num;                                          \
            conf.size = default_size;                                        \
        }                                                                    \
    }

#define ngx_conf_merge_bitmask_value(conf, prev, default)                    \
    if (conf == 0) {                                                         \
        conf = (prev == 0) ? default : prev;                                 \
    }


char *ngx_conf_param(ngx_conf_t *cf);

char *ngx_conf_parse(ngx_conf_t *cf, ngx_str_t *filename);

char *ngx_conf_include(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);


ngx_int_t ngx_conf_full_name(ngx_cycle_t *cycle, ngx_str_t *name,
                             ngx_uint_t conf_prefix);

ngx_open_file_t *ngx_conf_open_file(ngx_cycle_t *cycle, ngx_str_t *name);

void ngx_cdecl ngx_conf_log_error(ngx_uint_t level, ngx_conf_t *cf,
                                  ngx_err_t err, const char *fmt, ...);


char *ngx_conf_set_flag_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

char *ngx_conf_set_str_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

char *ngx_conf_set_str_array_slot(ngx_conf_t *cf, ngx_command_t *cmd,
                                  void *conf);

char *ngx_conf_set_keyval_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

char *ngx_conf_set_num_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

char *ngx_conf_set_size_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

char *ngx_conf_set_off_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

char *ngx_conf_set_msec_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

char *ngx_conf_set_sec_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

char *ngx_conf_set_bufs_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

char *ngx_conf_set_enum_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

char *ngx_conf_set_bitmask_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);


#endif /* _NGX_CONF_FILE_H_INCLUDED_ */
