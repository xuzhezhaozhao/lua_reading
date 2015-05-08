/*
** $Id: ltable.h,v 2.20 2014/09/04 18:15:29 roberto Exp $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/

#ifndef ltable_h
#define ltable_h

#include "lobject.h"


/* t table, 返回 node 数组 i 位置的地址 */
#define gnode(t,i)	(&(t)->node[i])
/* n 为node, 获取 node 的属性值的地址 */
#define gval(n)		(&(n)->i_val)
#define gnext(n)	((n)->i_key.nk.next)


/* 'const' to avoid wrong writings that can mess up field 'next' */ 
/* n 为 node 类型, 获取 key 的值 */
#define gkey(n)		cast(const TValue*, (&(n)->i_key.tvk))

#define wgkey(n)		(&(n)->i_key.nk)

#define invalidateTMcache(t)	((t)->flags = 0)


/* returns the key, given the value of a table entry */
#define keyfromval(v) \
  (gkey(cast(Node *, cast(char *, (v)) - offsetof(Node, i_val))))


/*
** search function for integers
*/
/**
 * 在Table中查找属性 key 的值, key 是一个整数类型，存在返回其值，否则返回 nil;
 * lua中 table 模拟的数组下标是1开始的, 若key范围在table模拟的array内，则会返回
 * array对应位置的值，否则会在node数组中查找
 */
LUAI_FUNC const TValue *luaH_getint (Table *t, lua_Integer key);
/**
 * t[key] = value
 * 向表中插入 key-value 对, key 存在就用 value 覆盖原来的值, 否则就是新建对
 */
LUAI_FUNC void luaH_setint (lua_State *L, Table *t, lua_Integer key,
                                                    TValue *value);
LUAI_FUNC const TValue *luaH_getstr (Table *t, TString *key);
/* 返回 t[key], 不触发元方法 */
LUAI_FUNC const TValue *luaH_get (Table *t, const TValue *key);
LUAI_FUNC TValue *luaH_newkey (lua_State *L, Table *t, const TValue *key);
/*
** beware: when using this function you probably need to check a GC
** barrier and invalidate the TM cache.
*/
/**
 * 返回 key 对应的值的地址, 若表中没有key, 则会新建这个key 
 */
LUAI_FUNC TValue *luaH_set (lua_State *L, Table *t, const TValue *key);
/*  新建 Table */
LUAI_FUNC Table *luaH_new (lua_State *L);
/**
 * table sequence 的大小改为 nasize，node hash 表的大小改为 nhsize;
 */
LUAI_FUNC void luaH_resize (lua_State *L, Table *t, unsigned int nasize,
                                                    unsigned int nhsize);
LUAI_FUNC void luaH_resizearray (lua_State *L, Table *t, unsigned int nasize);
LUAI_FUNC void luaH_free (lua_State *L, Table *t);
/**
 * 首先查找 key 在 table 中的索引顺序位置，查找顺序是先array，再 
 * node hash table, 然后将这个位置后的第一个非 nil 元素的 key, value 
 * 分别存入 key 和 key + 1 两个位置, 对于 array 来说，key 就是数组下标加 1.
 * 
 * key 如果是栈顶位置的话，新压入的 key, value 对就在栈顶.
 * 
 * 如果找到非 nil 元素，返回 1, 否则返回 0.
 */
LUAI_FUNC int luaH_next (lua_State *L, Table *t, StkId key);
/*
** Try to find a boundary in table 't'. A 'boundary' is an integer index
** such that t[i] is non-nil and t[i+1] is nil (and 0 if t[1] is nil).
*/
/* 先在 array 中找，再在 hash part 中找 */ 
LUAI_FUNC int luaH_getn (Table *t);


#if defined(LUA_DEBUG)
LUAI_FUNC Node *luaH_mainposition (const Table *t, const TValue *key);
LUAI_FUNC int luaH_isdummy (Node *n);
#endif


#endif
