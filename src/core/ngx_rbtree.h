
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_RBTREE_H_INCLUDED_
#define _NGX_RBTREE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

typedef ngx_uint_t ngx_rbtree_key_t;
typedef ngx_int_t ngx_rbtree_key_int_t;


typedef struct ngx_rbtree_node_s ngx_rbtree_node_t;
/*ngx_rbtree_node_t是红黑树实现中必须用到的数据结构,一般我们把它放到结构体中的
第1个成员中,这样方便把自定义的结构体强制转换成ngx_rbtree_node_t类型.例如:
typedef struct  {
    //一般都将ngx_rbtree_node_t节点结构体放在自走义数据类型的第1位,以方便类型的强制转换
    ngx_rbtree_node_t node;
    ngx_uint_t num;
} TestRBTreeNode;
    如果这里希望容器中元素的数据类型是TestRBTreeNode,那么只需要在第1个成员中
放上ngx_rbtree_node_t类型的node即可.在调用图7-7中ngx_rbtree_t容器所提供的方法
时,需要的参数都是ngx_rbtree_node_t类型,这时将TestRBTreeNode类型的指针强制转换
成ngx_rbtree_node_t即可*/
struct ngx_rbtree_node_s {
    /* key成员是每个红黑树节点的关键字,它必须是整型.红黑树的排序主要依据key成员 */
    ngx_rbtree_key_t key; //无符号整型的关键字  参考ngx_http_file_cache_exists  其实就是ngx_http_cache_t->key的前4字节
    ngx_rbtree_node_t *left;
    ngx_rbtree_node_t *right;
    ngx_rbtree_node_t *parent;
    u_char color; //节点的颜色,0表示黑色,l表示红色
    u_char data; //仅1个字节的节点数据.由于表示的空间太小,所以一般很少使用
};


typedef struct ngx_rbtree_s ngx_rbtree_t;

/*红黑树是一个通用的数据结构,它的节点(或者称为容器的元素)可以是包含基本红黑树节点的任意结构体.对于不同的结构体,很多场合下是允许不同的节点拥有相同的关键字的().
例如,不同的字符串可能会散列出相同的关键字,这时它们在红黑树中的关键字是相同的,然而它们又是不同的节点,这样在添加时就不可以覆盖原有同名关键字节点,而是作为新插入的节点存在.
因此,在添加元素时,需要考虑到这种情况.将添加元素的方法抽象出ngx_rbtree_insert_pt函数指针可以很好地实现这一思想,用户也可以灵活的定义自己的行为.
Nginx帮助用户实现了3种简单行为的添加节点方法,为解决不同节点含有相同关键字的元素冲突问题,红黑树设置了ngx_rbtree_insert_pt指针,这样可灵活地添加冲突元素*/

typedef void (*ngx_rbtree_insert_pt)(ngx_rbtree_node_t *root,
                                     ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel);

struct ngx_rbtree_s {
    ngx_rbtree_node_t     *root;      //指向树的根节点.注意,根节点也是数据元素
    ngx_rbtree_node_t     *sentinel;  //指向NIL峭兵节点,哨兵节点是所有最下层的叶子节点都指向一个NULL空节点
    ngx_rbtree_insert_pt   insert;    //表示红黑树添加元素的函数指针,它决定在添加新节点时的行为究竟是替换还是新增
};

//sentinel哨兵代表外部节点,所有的叶子以及根部的父节点,都指向这个唯一的哨兵nil,哨兵的颜色为黑色
#define ngx_rbtree_init(tree, s, i)                                           \
    ngx_rbtree_sentinel_init(s);                                              \
    (tree)->root = s;                                                         \
    (tree)->sentinel = s;                                                     \
    (tree)->insert = i

#define ngx_rbtree_data(node, type, link)                                     \
    (type *) ((u_char *) (node) - offsetof(type, link))


void ngx_rbtree_insert(ngx_rbtree_t *tree, ngx_rbtree_node_t *node);

void ngx_rbtree_delete(ngx_rbtree_t *tree, ngx_rbtree_node_t *node);

void ngx_rbtree_insert_value(ngx_rbtree_node_t *root, ngx_rbtree_node_t *node,
                             ngx_rbtree_node_t *sentinel);

void ngx_rbtree_insert_timer_value(ngx_rbtree_node_t *root,
                                   ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel);

ngx_rbtree_node_t *ngx_rbtree_next(ngx_rbtree_t *tree,
                                   ngx_rbtree_node_t *node);


#define ngx_rbt_red(node)               ((node)->color = 1)
#define ngx_rbt_black(node)             ((node)->color = 0)
#define ngx_rbt_is_red(node)            ((node)->color)
#define ngx_rbt_is_black(node)          (!ngx_rbt_is_red(node))
#define ngx_rbt_copy_color(n1, n2)      (n1->color = n2->color)


/* a sentinel must be black */

#define ngx_rbtree_sentinel_init(node)  ngx_rbt_black(node)


static ngx_inline ngx_rbtree_node_t *
ngx_rbtree_min(ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel) {
    while (node->left != sentinel) {
        node = node->left;
    }

    return node;
}


#endif /* _NGX_RBTREE_H_INCLUDED_ */
