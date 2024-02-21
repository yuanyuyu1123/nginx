
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

/*
总结，一般一级配置(http{}外的配置项)一般属性包括NGX_MAIN_CONF|NGX_DIRECT_CONF。http events等这一行的配置属性,全局配置项worker_priority等也属于这个行列
包括NGX_MAIN_CONF不包括NGX_DIRECT_CONF
*/
#define NGX_MAIN_CONF        0x01000000

/*
配置类型模块是唯一一种只有1个模块的模块类型。配置模块的类型叫做NGX_CONF_MODULE，它仅有的模块叫做ngx_conf_module，这是Nginx最
底层的模块，它指导着所有模块以配置项为核心来提供功能。因此，它是其他所有模块的基础。
*/
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
/*
NGX_CORE_MODULE主要包括以下模块:
ngx_core_module  ngx_events_module  ngx_http_module  ngx_errlog_module  ngx_mail_module
ngx_regex_module  ngx_stream_module  ngx_thread_pool_module
*/

//所有的核心模块NGX_CORE_MODULE对应的上下文ctx为ngx_core_module_t，子模块，例如http{} NGX_HTTP_MODULE模块对应的为上下文为ngx_http_module_t
//events{} NGX_EVENT_MODULE模块对应的为上下文为ngx_event_module_t
/*
Nginx还定义了一种基础类型的模块：核心模块，它的模块类型叫做NGX_CORE_MODULE。目前官方的核心类型模块中共有6个具体模块，分别
是ngx_core_module、ngx_errlog_module、ngx_events_module、ngx_openssl_module、ngx_http_module、ngx_mail_module模块
*/
#define NGX_CORE_MODULE      0x45524F43  /* "CORE" */ //二级模块类型http模块个数，见ngx_http_block  ngx_max_module为NGX_CORE_MODULE(一级模块类型)类型的模块数
//NGX_CONF_MODULE只包括:ngx_conf_module
#define NGX_CONF_MODULE      0x464E4F43  /* "CONF" */


#define NGX_MAX_CONF_ERRSTR  1024

/*
表4-1  ngx_command_s结构体中type成员的取值及其意义
┏━━━━━━━━━┳━━━━━━━━━━┳━━━━━━━━━━━━━━━━━━━━━━━━━━┓
┃    type类型      ┃    type取值        ┃    意义                                            ┃
┣━━━━━━━━━╋━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃                    ┃  一般由NGX—CORE—MODULE类型的核心模块使用，       ┃
┃                  ┃                    ┃仅与下面的NGX_ MAIN—CONF同时设置，表示模块需       ┃
┃  处理配置项时获  ┃NGX_DIRECT_CONF     ┃要解析不属于任何{）内的全局配置项。它实际上会指定   ┃
┃取当前配置块的方  ┃                    ┃set方法里的第3个参数conf的值，使之指向每个模块解    ┃
┃式                ┃                    ┃析全局配置项的配置结构体①                          ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃NGX_ANY_CONF        ┃  目前未使用，设置与否均无意义                      ┃
┣━━━━━━━━━╋━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃                    ┃  配置项可以出现在全局配置中，即不属于任何{）配置   ┃
┃                  ┃NGX_MAIN_CONF       ┃                                                    ┃
┃                  ┃                    ┃块                                                  ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃NGX_EVENT_CONF      ┃  配置项可以出现在events{}块内                      ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃NGX_MAIL_MAIN_CONF  ┃  配置项可以出现在mail{}块或者imap{）块内           ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃                    ┃  配置项可以出现在server{}块内，然而该server{}块    ┃
┃                  ┃NGX_MAIL_SRV_CONF   ┃                                                    ┃
┃                  ┃                    ┃必须属于mail{}块或者imap{}块                        ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃NGX_HTTP_MAIN_CONF  ┃  配置项可以出现在http{}块内                        ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃NGX_HTTP_SRV_CONF   ┃  配置项可以出现在server{）块肉，然而该server块必   ┃
┃                  ┃                    ┃须属于http{）块                                     ┃
┃  配置项可以在哪  ┃                    ┃                                                    ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃                    ┃  配置项可以出现在location{}块内，然而该location块  ┃
┃些{）配置块中出现 ┃NGX_HTTP_LOC_CONF   ┃                                                    ┃
┃                  ┃                    ┃必须属于http{）块                                   ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃                    ┃  配置项可以出现在upstream{}块内，然而该upstream    ┃
┃                  ┃NGX_HTTP_UPS_CONF   ┃                                                    ┃
┃                  ┃                    ┃块必须属于http{）块                                 ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃                    ┃  配置项可以出现在server块内的if{}块中。目前仅有    ┃
┃                  ┃NGX_HTTP_SIF_CONF   ┃                                                    ┃
┃                  ┃                    ┃rewrite模块会使用，该if块必须属于http{）块          ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃                    ┃  配置项可以出现在location块内的if{）块中。目前仅   ┃
┃                  ┃NGX_HTTP_LIF_CONF   ┃                                                    ┃
┃                  ┃                    ┃有rewrite模块会使用，该if块必须属于http{）块        ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃                    ┃  配置项可以出现在limit_except{）块内，然而该limit- ┃
┃                  ┃NGX_HTTP_LMT_CONF   ┃                                                    ┃
┃                  ┃                    ┃except块必须属于http{）块                           ┃
┣━━━━━━━━━╋━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃NGX_CONF_NOARGS     ┃  配置项不携带任何参数                              ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃NGX_CONF_TAKE1      ┃  配置项必须携带1个参数                             ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃NGX_CONF_TAKE2      ┃  配置项必须携带2个参数                             ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃NGX_CONF_TAKE3      ┃  配置项必须携带3个参数                             ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃NGX_CONF_TAKE4      ┃  配置项必须携带4个参数                             ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃NGX_CONF_TAKE5      ┃  配置项必须携带5个参数                             ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃  限制配置项的参  ┃NGX_CONF_TAKE6      ┃  酡置项必须携带6个参数                             ┃
┃数个数            ┃                    ┃                                                    ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃NGX_CONF_TAKE7      ┃  配置项必须携带7个参数                             ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃NGX_CONF_TAKE12     ┃  配置项可以携带1个参数或2个参数                    ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃NGX_CONF_TAKE13     ┃  配置项可以携带1个参数或3个参数                    ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃NGX_CONF_TAKE23     ┃  配置项可以携带2个参数或3个参数                    ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃NGX_CONF_TAKE123    ┃  配置项可以携带1～3个参数                          ┃
┃                  ┣━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃NGX_CONF_TAKE1234   ┃  配置项可以携带1～4个参数                          ┃
┗━━━━━━━━━┻━━━━━━━━━━┻━━━━━━━━━━━━━━━━━━━━━━━━━━┛
┏━━━━━━━━━┳━━━━━━━━━━━┳━━━━━━━━━━━━━━━━━━━━━━━━━━┓
┃    type类型      ┃    type取值          ┃    意义                                            ┃
┣━━━━━━━━━╋━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃NGX CONF ARGS NUMBER  ┃  目前未使用，无意义                                ┃
┃                  ┣━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃                      ┃  配置项定义了一种新的{）块。例如，http、server、   ┃
┃                  ┃NGX CONF BLOCK        ┃location等配置，它们的type都必须定义为NGX二CONF     ┃
┃                  ┃                      ┃ BLOCK                                              ┃
┃                  ┣━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃ NGX CONF ANY         ┃  不验证配置项携带的参数个数                        ┃
┃                  ┣━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃                      ┃  配置项携带的参数只能是1个，并且参数的值只能是     ┃
┃                  ┃NGX CONF FLAG         ┃                                                    ┃
┃                  ┃                      ┃on或者off                                           ┃
┃                  ┣━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃NGX CONF IMORE        ┃  配置项携带的参数个数必须超过1个                   ┃
┃                  ┣━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                  ┃NGX CONF 2MORE        ┃  配置项携带的参数个数必须超过2个                   ┃
┃  限制配置项后的  ┃                      ┃                                                    ┃
┃                  ┣━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃参数出现的形式    ┃                      ┃  表示当前配置项可以出现在任意块中（包括不属于任    ┃
┃                  ┃                      ┃何块的全局配置），它仅用于配合其他配置项使用。type  ┃
┃                  ┃                      ┃中未加NGX—CONF_ MULTI时，如果一个配置项出现在      ┃
┃                  ┃                      ┃type成员未标明的配置块中，那么Nginx会认为该配置     ┃
┃                  ┃                      ┃项非法，最后将导致Nginx启动失败。但如果type中加     ┃
┃                  ┃                      ┃入了NGX CONF- MULTI，则认为该配置项一定是合法       ┃
┃                  ┃NGX CONF MULTI        ┃                                                    ┃
┃                  ┃                      ┃的，然而又会有两种不同的结果：①如果配置项出现在    ┃
┃                  ┃                      ┃type指示的块中，则会调用set方法解析醌置项；②如果   ┃
┃                  ┃                      ┃配置项没有出现在type指示的块中，则不对该配置项做    ┃
┃                  ┃                      ┃任何处理。因此，NGX—CONF—MULTI会使得配置项出      ┃
┃                  ┃                      ┃现在未知块中时不会出错。目前，还没有官方模块使用过  ┃
┃                  ┃                      ┃NGX—CONF—MULTI                                    ┃
┗━━━━━━━━━┻━━━━━━━━━━━┻━━━━━━━━━━━━━━━━━━━━━━━━━━┛
①每个进程中都有一个唯一的ngx_cycle_t核心结构体，它有一个成员conf ctx维护着所有模块的配置结构体，
  其类型是voi扩”4。conf ctx意义为首先指向一个成员皆为指针的数组，其中每个成员指针又指向另外一个
  成员皆为指针的数组，第2个子数组中的成员指针才会指向各模块生成的配置结构体。这正是为了事件模
  块、http模块、mail模块而设计的，这有利于不同于NGX CORE MODULE类型的
  特定模块解析配置项。然而，NGX CORE—MODULE类型的核心模块解析配置项时，配置项一定是全局的，
  不会从属于任何{）配置块的，它不需要上述这种双数组设计。解析标识为NGX DIRECT CONF类型的配
  置项时，会把void+十++类型的conf ctx强制转换为void~+，也就是说，此时，在conf ctx指向的指针数组
  中，每个成员指针不再指向其他数组，直接指向核心模块生成的配置鲒构体。因此，NGX_ DIRECT__ CONF
  仅由NGX CORE MODULE类型的核心模块使用，而且配置项只应该出现在全局配置中。
    注意  如果HTTP模块中定义的配置项在nginx.conf配置文件中实际出现的位置和参数格式与type的意义不符，那么Nginx在启动时会报错。
char*(*set)(ngx_conf_t *cf, ngx_commandj 'vcmd,void *conf)
    关于set回调方法，在处理mytest配置项时已经使用过，其中mytest配置项是
不带参数的。如果处理配置项，我们既可以自己实现一个回调方法来处理配置项（），也可以使用Nginx预设的14个解析配置项方法，这会少
写许多代码，
┏━━━━━━━━━━━━━━┳━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
┃    预设方法名              ┃    行为                                                                      ┃
┣━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                            ┃  如果nginx．conf文件中某个配置项的参数是on或者off（即希望配置项表达打开      ┃
┃                            ┃或者关闭某个功能的意思），而且在Nginx模块的代码中使用ngx_flag_t变量来保       ┃
┃ngx_conf_set_flag_slot      ┃存这个配置项的参数，就可以将set回调方法设为ngx_conf_set_flag_slot。当nginx.   ┃
┃                            ┃conf文件中参数是on时，代码中的ngx_flag_t类型变量将设为1，参数为off时则        ┃
┃                            ┃设为0                                                                         ┃
┣━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                            ┃  如果配置项后只有1个参数，同时在代码中我们希望用ngx_str_t类型的变量来保      ┃
┃ngx_conf_set_str_slot       ┃                                                                              ┃
┃                            ┃存这个配置项的参数，则可以使用ngx_conf_set_ str_slot方法                      ┃
┣━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                            ┃  如果这个配置项会出现多次，每个配置项后面都跟着1个参数，而在程序中我们       ┃
┃                            ┃希望仅用一个ngx_array_t动态数组（           ）来存储所有的参数，且数组中      ┃
┃ngx_conf_set_str_array_slot ┃                                                                              ┃
┃                            ┃的每个参数都以ngx_str_t来存储，那么预设的ngx_conf_set_str_array_slot有法可    ┃
┃                            ┃以帮我们做到                                                                  ┃
┣━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                            ┃  与ngx_conf_set_str_array_slot类似，也是用一个ngx_array_t数组来存储所有同    ┃
┃                            ┃名配置项的参数。只是每个配置项的参数不再只是1个，而必须是两个，且以“配       ┃
┃ngx_conf_set_keyval_slot    ┃                                                                              ┃
┃                            ┃置项名关键字值；”的形式出现在nginx．conf文件中，同时，ngx_conf_set_keyval    ┃
┃                            ┃ slot将把这蜱配置项转化为数组，其中每个元素都存储着key/value键值对            ┃
┣━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃ngx_conf_set_num_slot       ┃  配置项后必须携带1个参数，且只能是数字。存储这个参数的变量必须是整型         ┃
┣━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                            ┃  配置项后必须携带1个参数，表示空间大小，可以是一个数字，这时表示字节数       ┃
┃                            ┃(Byte)。如果数字后跟着k或者K，就表示Kilobyt，IKB=1024B；如果数字后跟          ┃
┃ngx_conf_set_size_slot      ┃                                                                              ┃
┃                            ┃着m或者M，就表示Megabyte，1MB=1024KB。ngx_conf_set_ size slot解析后将         ┃
┃                            ┃把配置项后的参数转化成以字节数为单位的数字                                    ┃
┣━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                            ┃  配置项后必须携带1个参数，表不空间上的偏移量。它与设置的参数非常炎似，       ┃
┃                            ┃其参数是一个数字时表示Byte，也可以在后面加单位，但与ngx_conf_set_size slot    ┃
┃ngx_conf_set_off_slot       ┃不同的是，数字后面的单位不仅可以是k或者K、m或者M，还可以是g或者G，            ┃
┃                            ┃这时表示Gigabyte，IGB=1024MB。ngx_conf_set_off slot解析后将把配置项后的       ┃
┃                            ┃参数转化成以字节数为单位的数字                                                ┃
┣━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                            ┃  配置项后必须携带1个参数，表示时间。这个参数可以在数字后面加单位，如         ┃
┃                            ┃果单位为s或者没有任何单位，那么这个数字表示秒；如果单位为m，则表示分          ┃
┃                            ┃钟，Im=60s：如果单位为h，则表示小时，th=60m：如果单位为d，则表示天，          ┃
┃ngx_conf_set_msec_slot      ┃                                                                              ┃
┃                            ┃ld=24h：如果单位为w，则表示周，lw=7d；如果单位为M，则表示月，1M=30d；         ┃
┃                            ┃如果单位为y，则表示年，ly=365d。ngx_conf_set_msec—slot解析后将把配置项后     ┃
┃                            ┃的参数转化成以毫秒为单位的数字                                                ┃
┣━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                            ┃  与ngx_conf_set_msec slot非常类似，唯一的区别是ngx_conf_set msec—slot解析   ┃
┃ngx_conf_set_sec_slot       ┃后将把配置项后的参数转化成以毫秒为单位的数字，而ngx_conf_set_sec一slot解析    ┃
┃                            ┃后会把配置项后的参数转化成以秒为单位的数字                                    ┃
┣━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                            ┃  配置项后必须携带一两个参数，第1个参数是数字，第2个参数表示空间大小。        ┃
┃                            ┃例如，“gzip_buffers 4 8k;”（通常用来表示有多少个ngx_buf_t缓冲区），其中第1  ┃
┃                            ┃个参数不可以携带任何单位，第2个参数不带任何单位时表示Byte，如果以k或          ┃
┃ngx_conf_set_bufs_slot      ┃者K作为单位，则表示Kilobyte，如果以m或者M作为单位，则表示Megabyte。           ┃
┃                            ┃ngx_conf_set- bufs—slot解析后会把配置项后的两个参数转化成ngx_bufs_t结构体下  ┃
┃                            ┃的两个成员。这个配置项对应于Nginx最喜欢用的多缓冲区的解决方案（如接收连       ┃
┃                            ┃接对端发来的TCP流）                                                           ┃
┣━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                            ┃  配置项后必须携带1个参数，其取值范围必须是我们设定好的字符串之一（就像       ┃
┃                            ┃C语言中的枚举一样）。首先，我们要用ngx_conf_enum_t结构定义配置项的取值范      ┃
┃ngx_conf_set_enum slot      ┃                                                                              ┃
┃                            ┃围，并设定每个值对应的序列号。然后，ngx_conf_set_enum_slot将会把配置项参      ┃
┃                            ┃数转化为对应的序列号                                                          ┃
┣━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃                            ┃  写ngx_conf_set_enum_slot类似，配置项后必须携带1个参数，其取值范围必      ┃
┃                            ┃须是设定好的字符串之一。首先，我们要用ngx_conf_bitmask_t结构定义配置项的      ┃
┃ngx_conf_set_bitmask_slot   ┃                                                                              ┃
┃                            ┃取值范围，并设定每个值对应的比特位。注意，每个值所对应的比特位都要不同。      ┃
┃                            ┃然后ngx_conf_set_bitmask_ slot将会把配置项参数转化为对应的比特位              ┃
┗━━━━━━━━━━━━━━┻━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
ngx_uint_t  conf
    conf用于指示配置项所处内存的相对偏移位置，仅在type中没有设置NGX_DIRECT_CONF和NGX_MAIN_CONF时才会生效。
	对于HTTP模块，conf是必须要设置的，它的取值范围见表4-3。
表4-3  ngx_commandj结构中的conf成员在HTTP模块中的取僮及其意义
┏━━━━━━━━━━━━━━┳━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
┃    conf在HTTP模块中的取值  ┃    意义                                                          ┃
┣━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃NGX_HTTP_MAIN_CONF_OFFSET   ┃  使用create_main_conf方法产生的结构体来存储解析出的配置项参数    ┃
┣━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃NGX_HTTP_SRV_CONF_OFFSET    ┃  使用create_srv_conf方法产生的结构体来存储解析出的配置项参数     ┃
┣━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫
┃NGX_HTTP_LOC_CONF_OFFSET    ┃  使用create_loc_conf方法产生的结构体来存储解析出的配置项参数     ┃
┗━━━━━━━━━━━━━━┻━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
    为什么HTTP模块一定要设置conf的值呢？因为HTTP框架可以使用预设的14种方法
自动地将解析出的配置项写入HTTP模块代码定义的结构体中，但HTTP模块中可能会定义
3个结构体，分别用于存储main、srv、loc级别的配置项（对应于create_main_conf、create_srv_conf、
create_loc_conf方法创建的结构体），而HTTP框架自动解析时需要知道应把解析
出的配置项值写入哪个结构体中，这将由conf成员完成。
    因此，对conf的设置是与ngx_http_module_t实现的回调方法（ ）相
关的。如果用于存储这个配置项的数据结构是由create_main_conf回调方法完成的，那么必
须把conf设置为NGX_HTTP_MAIN_CONF_OFFSET。同样，如果这个配置项所属的数据结
构是由create_srv_conf回调方法完成的，那么必须把conf设置为NGX_HTTP_SRV_CONF_OFFSET。
可如果create_loc_conf负责生成存储这个配置项的数据结构，就得将conf设置为
NGX_HTTP_LOC_CONF_OFFSET。
    目前，功能较为简单的HTTP模块都只实现了create_loc_conf回调方法，对于http{）、
server{}块内出现的同名配置项，都是并入某个location{）内create_loc_conf方法产生的结
构体中的（  ）。当我们希望同时出现在http{）、server{）、
location{)块的同名配置项，在HTTP模块的代码中保存于不同的变量中时，就需要实现
create_main_conf方法、create_srv_conf方法产生新的结构体，从而以不同的结构体独立保存不
同级别的配置项，而不是全部合并到某个location下create_loc_conf方法生戍的结构体中。
ngx_uint_t offset
  offset表示当前配置项在整个存储配置项的结构体中的偏移位置(以字节(Byte)为单
位）。举个例子，在32位机器上，int（整型）类型长度是4字节，那么看下面这个数据结构：
	typedef struct {
		int a;
		int b;
		int c;
	} test_stru;
    如果要处理的配置项是由成员b来存储参数的，那么这时b相对于test_stru的偏移量
就是4;如果要处理的配置项由成员c来存储参数，那么这时c相对于test_stru的偏移量
就是8。
    实际上，这种计算工作不用用户自己来做，使用offsetof宏即可实现。例如，在上例中
取b的偏移量时可以这么做：
    其中，offsetof中第1个参数是存储配置项的结构体名称，第2个参数是这个结构体中
的变量名称。offsetof将会返回这个变量相对于结构体的偏移量。
    提示offsetof这个宏是如何取得成员相对结构体的偏移量的呢？其实很简单，它的
实现类似于：#define offsetof(type，member) (size_t)&(((type *)0)->member)。可以看到，
offsetof将0地址转换成type结构体类型的指针，并在访问member成员时取得member咸员
的指针，这个指针相对于0地址来说自然就是成员相对于结构体的偏移量了。
    设置offset有什么作用呢？如果使用Nginx预设的解析配置项方法，就必须设置offset，
这样Nginx首先通过conf成员找到应该用哪个结构体来存放，然后通过offset成员找到这个
结构体中的相应成员，以便存放该配置。如果是自定义的专用配置项解析方法（只解析某一
个配置项），则可以不设置offset的值。
(6) void  *post
    post指针有许多用途，从它被设计成void *就可以看出。
    如果自定义了配置项的回调方法，那么post指针的用途完全由用户来定义。如果不使
用它，那么随意设为NULL即可。如果想将一些数据结构或者方法的指针传过来，那么使用
post也可以。
    如果使用Nginx预设的配置项解析方法，就需要根据这些预设方法来决定post的使用方
式。表4-4说明了post相对于14个预设方法的用途。
表4-4 ngx_commandj结构中post的取值及其意义
┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┳━━━━━━━━━━━━━━┓
┃    post的使用方式                                                        ┃  适用的预设配置项解析方法  ┃
┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━┫
┃                                                                          ┃ngx_conf_set_flag_slot      ┃
┃                                                                          ┣━━━━━━━━━━━━━━┫
┃                                                                          ┃ngx_conf_set str slot       ┃
┃                                                                          ┣━━━━━━━━━━━━━━┫
┃                                                                          ┃ngx_conf_set_str_array_slot ┃
┃                                                                          ┣━━━━━━━━━━━━━━┫
┃  可以选择是否实现。如果设为NULL，则表示不实现，否则必须实现为指          ┃ngx_conf_set_keyval_slot    ┃
┃                                                                          ┣━━━━━━━━━━━━━━┫
┃向ngx_conf_post_t结构的指针。ngx_conf_post_t中包含一个方法指针，表示      ┃ngx_conf_set num slot       ┃
┃在解析当前配置项完毕后，需要回调这个方法                                  ┃                            ┃
┃                                                                          ┣━━━━━━━━━━━━━━┫
┃                                                                          ┃ngx_conf_set size slot      ┃
┃                                                                          ┣━━━━━━━━━━━━━━┫
┃                                                                          ┃ngx_conf_set_off slot       ┃
┃                                                                          ┣━━━━━━━━━━━━━━┫
┃                                                                          ┃ngx_conf_set msec slot      ┃
┃                                                                          ┣━━━━━━━━━━━━━━┫
┃                                                                          ┃ngx_conf_set sec slot       ┃
┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━┫
┃  指向ngx_conf_enum_t数组，表示当前配置项的参数必须设置为ngx_conf_        ┃                            ┃
┃enumt规定的值（类似枚举）。注意，使用ngx_conf_set_enum_slot时必须设       ┃ngx_conf_set_enum_slot      ┃
┃置定义1个ngx_conf_enum_t数组，并将post成员指向该数组                      ┃                            ┃
┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━┫
┃  揩向ngx_conf_bitmask_t数组，表示当前配置项的参数必须设置为ngx_          ┃                            ┃
┃conf_bitmask_t规定的值（类似枚举）。注意，使用ngx_conf_set bitmask slot   ┃ngx_conf_set_bitmask_slot   ┃
┃时必须设置定义1个ngx_conf_bitmask_t数组，并将post成员指向该数组           ┃                            ┃
┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━┫
┃                                                                          ┃ngx_conf_set bufs slot      ┃
┃                                                                          ┣━━━━━━━━━━━━━━┫
┃  无任何用处                                                              ┃ngx_conf_set_path_slot      ┃
┃                                                                          ┣━━━━━━━━━━━━━━┫
┃                                                                          ┃ngx_conf_set access slot    ┃
┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┻━━━━━━━━━━━━━━┛
    可以看到，有9个预设方法在使用时post是可以设置为ngx_conf_post_t结构体来使用
的，先来看看ngx_conf_post_t的定义。
typedef char *(*ngx_conf_post_handler_pt) (ngx_conf_t *cf,
    void *data, void *conf);
typedef struct {
    ngx_conf_post_handler_pt  post_handler;
} ngx_conf_post_t;
    如果需要在解析完配置项（表4-4中列出的前9个预设方法）后回调某个方法，就要实
现上面昀ngx_conf_post_handler_pt，并将包含post_handler的ngx_conf_post_t结构体传给
post指针。
    目前，ngx_conf_post_t结构体提供的这个功能没有官方Nginx模块使用，因为它限制过
多且post成员过于灵活，一般完全可以init main conf这样的方法统一处理解析完的配置项。
*/

/*
commands数组用于定义模块的配置文件参数，每一个数组元素都是ngx_command_t类型，数组的结尾用ngx_null_command表示。Nginx在解析配置
文件中的一个配置项时首先会遍历所有的模块，对于每一个模块而言，即通过遍历commands数组进行，另外，在数组中检查到ngx_null_command时，
会停止使用当前模块解析该配置项。每一个ngx_command_t结构体定义了自己感兴趣的一个配置项：
typedef struct ngx_command_s     ngx_command_t;
*/ //每个module都有自己的command,见ngx_modules中对应模块的command.每个进程中都有一个唯一的ngx_cycle_t核心结构体,它有一个成员conf_ctx维护着所有模块的配置结构体
struct ngx_command_s { //所有配置的最初源头在ngx_init_cycle
    ngx_str_t name; //配置项名称，如"gzip"
    /*配置项类型，type将指定配置项可以出现的位置。例如，出现在server{}或location{}中，以及它可以携带的参数个数*/
    /*type决定这个配置项可以在哪些块（如http、server、location、if、upstream块等）
      中出现，以及可以携带的参数类型和个数等。
      注意，type可以同时取多个值，各值之间用|符号连接，例如，type可以取
      值为NGX_TTP_MAIN_CONF | NGX_HTTP_SRV_CONFI | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE。*/
    ngx_uint_t type; //取值可能为NGX_HTTP_LOC_CONF | NGX_CONF_TAKE2等

    //出现了name中指定的配置项后，将会调用set方法处理配置项的参数
    //cf里面存储的是从配置文件里面解析出的内容，conf是最终用来存储解析内容的内存空间，cmd为存到空间的那个地方(使用偏移量来衡量)
    //在ngx_conf_parse解析完参数后，在ngx_conf_handler中执行
    char *(*set)(ngx_conf_t *cf, ngx_command_t *cmd, void *conf); //参考上面的图形化信息

    ngx_uint_t conf; //crate分配内存的时候的偏移量 NGX_HTTP_LOC_CONF_OFFSET NGX_HTTP_SRV_CONF_OFFSET
    /*通常用于使用预设的解析方法解析配置项，这是配置模块的一个优秀设计。它需要与conf配合使用*/
    ngx_uint_t offset;
    //如果使用Nginx预设的配置项解析方法，就需要根据这些预设方法来决定post的使用方式。表4-4说明了post相对于14个预设方法的用途。
    /*
    参考14个回调方法后面的
    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, fp);
    }
    */
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
/*
其中，name表示配置项后的参数只能与name指向的字符串相等，而value表示如果参
数中出现了name，ngx_conf_set enum slot方法将会把对应的value设置到存储的变量中。
*/
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

/*
事实上，Nginx预设的配置项合并方法有10个，它们的行为与上述的ngx_conf_merge_
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
