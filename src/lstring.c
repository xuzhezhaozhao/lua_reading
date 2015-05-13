/*
** $Id: lstring.c,v 2.45 2014/11/02 19:19:04 roberto Exp $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#define lstring_c
#define LUA_CORE

#include "lprefix.h"


#include <string.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"



/*
** Lua will use at most ~(2^LUAI_HASHLIMIT) bytes from a string to
** compute its hash
*/
#if !defined(LUAI_HASHLIMIT)
#define LUAI_HASHLIMIT		5
#endif


/*
** equality for long strings
*/
/**
 * 比较字符串是否相等
 */
int luaS_eqlngstr (TString *a, TString *b) {
  size_t len = a->len;
  lua_assert(a->tt == LUA_TLNGSTR && b->tt == LUA_TLNGSTR);
  return (a == b) ||  /* same instance or... */
    ((len == b->len) &&  /* equal length and ... */
     (memcmp(getstr(a), getstr(b), len) == 0));  /* equal contents */
}


/**
 * 计算字符串 hash 值
 */
unsigned int luaS_hash (const char *str, size_t l, unsigned int seed) {
  unsigned int h = seed ^ cast(unsigned int, l);
  size_t l1;
  size_t step = (l >> LUAI_HASHLIMIT) + 1;
  for (l1 = l; l1 >= step; l1 -= step)
    h = h ^ ((h<<5) + (h>>2) + cast_byte(str[l1 - 1]));
  return h;
}


/*
** resizes the string table
*/
/**
 * 还需要重新 hash 表中的字符串
 */
void luaS_resize (lua_State *L, int newsize) {
  int i;
  stringtable *tb = &G(L)->strt;
  if (newsize > tb->size) {  /* grow table if needed */
    Dlog("luaS_resize, %d in use, grow: old size is %d, new size %d.",tb->nuse, tb->size, newsize);
    luaM_reallocvector(L, tb->hash, tb->size, newsize, TString *);
    for (i = tb->size; i < newsize; i++)
      tb->hash[i] = NULL;
  }
  for (i = 0; i < tb->size; i++) {  /* rehash */
    TString *p = tb->hash[i];
    tb->hash[i] = NULL;
    while (p) {  /* for each node in the list */
      TString *hnext = p->hnext;  /* save next */
      unsigned int h = lmod(p->hash, newsize);  /* new position */
      p->hnext = tb->hash[h];  /* chain it */
      tb->hash[h] = p;
      p = hnext;
    }
  }
  if (newsize < tb->size) {  /* shrink table if needed */
    Dlog("luaS_resize, %d in use, shrink: old size is %d,  new size is %d.", tb->nuse, tb->size, newsize);
    /* vanishing slice should be empty */
    lua_assert(tb->hash[newsize] == NULL && tb->hash[tb->size - 1] == NULL);
    luaM_reallocvector(L, tb->hash, tb->size, newsize, TString *);
  }
  tb->size = newsize;
}



/*
** creates a new string object
*/
/**
 * str: c字符串位置
 * l: 字符串长度
 * tag: 对象类型, 低三位是non-variant标志，标识Lua基本类型, 其余位区分如
 * 长短字符串类型，整数、浮点数。
 * h: hash值, 这里其实传入的是 g->seed 值, 惰性计算 hash 值, TString 的 extra 
 * 为 0 表示还没有计算 hash 值.
 */
static TString *createstrobj (lua_State *L, const char *str, size_t l,
                              int tag, unsigned int h) {
  TString *ts;
  GCObject *o;
  size_t totalsize;  /* total size of TString object */
  totalsize = sizelstring(l);
  o = luaC_newobj(L, tag, totalsize);
  ts = gco2ts(o);
  ts->len = l;
  ts->hash = h;
  /* 还没有计算 hash 值 */
  ts->extra = 0;
  memcpy(getaddrstr(ts), str, l * sizeof(char));
  getaddrstr(ts)[l] = '\0';  /* ending 0 */
  return ts;
}


/**
 * 从 string table 中删除一个string
 */
void luaS_remove (lua_State *L, TString *ts) {
  stringtable *tb = &G(L)->strt;
  TString **p = &tb->hash[lmod(ts->hash, tb->size)];
  while (*p != ts)  /* find previous element */
    p = &(*p)->hnext;
  *p = (*p)->hnext;  /* remove element from its list */
  tb->nuse--;
}


/*
** checks whether short string exists and reuses it or creates a new one
*/
/**
 * l的长度小于 LUAI_MAXSHORTLEN 就是 short string;
 * luaS_newlstr 函数会调用此函数.
 * 
 * hash table 的冲突解决办法是链式.
 * 
 * 若查询的字符串不存在 hash table 中, 则会新建字符串并插入.
 * 当 hash table 中元素个数 >= table size 时, 会尝试扩大为 2 倍.
 * 
 * 返回查询的字符串.
 */
static TString *internshrstr (lua_State *L, const char *str, size_t l) {
  TString *ts;
  global_State *g = G(L);
  unsigned int h = luaS_hash(str, l, g->seed);
  TString **list = &g->strt.hash[lmod(h, g->strt.size)];
  for (ts = *list; ts != NULL; ts = ts->hnext) {
    if (l == ts->len &&
        (memcmp(str, getstr(ts), l * sizeof(char)) == 0)) {
      /* found! */
      if (isdead(g, ts))  /* dead (but not collected yet)? */
        changewhite(ts);  /* resurrect it */
      return ts;
    }
  }
  /* not found */
  if (g->strt.nuse >= g->strt.size && g->strt.size <= MAX_INT/2) {
	  /* string table扩容 */
    luaS_resize(L, g->strt.size * 2);
    list = &g->strt.hash[lmod(h, g->strt.size)];  /* recompute with new size */
  }
  /* 插入到链表头部 */
  ts = createstrobj(L, str, l, LUA_TSHRSTR, h);
  ts->hnext = *list;
  *list = ts;
  g->strt.nuse++;
  return ts;
}


/*
** new string (with explicit length)
*/
/**
 * 如果有未回收的 short string，则会直接回收利用. 对于 long string 则会
 * 直接新建一个 TString 对象.
 */
TString *luaS_newlstr (lua_State *L, const char *str, size_t l) {
  if (l <= LUAI_MAXSHORTLEN)  /* short string? */
    return internshrstr(L, str, l);
  else {
    if (l + 1 > (MAX_SIZE - sizeof(TString))/sizeof(char))
      luaM_toobig(L);
    return createstrobj(L, str, l, LUA_TLNGSTR, G(L)->seed);
  }
}


/*
** new zero-terminated string
*/
TString *luaS_new (lua_State *L, const char *str) {
  return luaS_newlstr(L, str, strlen(str));
}


/**
 * 分配一个大小为 s 的 Udata 数据块
 */
Udata *luaS_newudata (lua_State *L, size_t s) {
  Udata *u;
  GCObject *o;
  if (s > MAX_SIZE - sizeof(Udata))
    luaM_toobig(L);
  o = luaC_newobj(L, LUA_TUSERDATA, sizeludata(s));
  u = gco2u(o);
  u->len = s;
  u->metatable = NULL;
  /* 将该对象设为 nil  */
  setuservalue(L, u, luaO_nilobject);
  return u;
}

