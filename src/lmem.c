/*
** $Id: lmem.c,v 1.89 2014/11/02 19:33:33 roberto Exp $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/

#define lmem_c
#define LUA_CORE

#include "lprefix.h"


#include <stddef.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"



/*
** About the realloc function:
** void * frealloc (void *ud, void *ptr, size_t osize, size_t nsize);
** ('osize' is the old size, 'nsize' is the new size)
**
** * frealloc(ud, NULL, x, s) creates a new block of size 's' (no
** matter 'x').
**
** * frealloc(ud, p, x, 0) frees the block 'p'
** (in this specific case, frealloc must return NULL);
** particularly, frealloc(ud, NULL, 0, 0) does nothing
** (which is equivalent to free(NULL) in ISO C)
**
** frealloc returns NULL if it cannot create or reallocate the area
** (any reallocation to an equal or smaller size cannot fail!)
*/



#define MINSIZEARRAY	4



/**
 * 管理可变长数组的, 其主要策略是当数组空间不够时,扩大为原来的两倍, 
 * 无法增长则报错。
 *
 * size: 原来元素个数, 最后返回实际分配的元素个数
 * size_elems: 单个元素的大小
 * limit: 元素个数最大限制
 * what: 元素类型描述，出错打印错误信息用的
 */
void *luaM_growaux_ (lua_State *L, void *block, int *size, size_t size_elems,
                     int limit, const char *what) {
  void *newblock;
  int newsize;
  if (*size >= limit/2) {  /* cannot double it? */
    if (*size >= limit)  /* cannot grow even a little? */
      luaG_runerror(L, "too many %s (limit is %d)", what, limit);
    newsize = limit;  /* still have at least one free place */
  }
  else {
    newsize = (*size)*2;
    if (newsize < MINSIZEARRAY)
      newsize = MINSIZEARRAY;  /* minimum size */
  }
  newblock = luaM_reallocv(L, block, *size, newsize, size_elems);
  *size = newsize;  /* update only when everything else is OK */
  return newblock;
}


l_noret luaM_toobig (lua_State *L) {
  luaG_runerror(L, "memory allocation error: block too big");
}



/*
** generic allocation routine.
*/
/**
 * 调用此函数前，参数都经过检查, 保证了分配的内存块不会过大;
 * 
 * 创建对象的函数 luaM_newobject 也调用此函数(lmem.h):
 *  #define luaM_newobject(L,tag,s)	luaM_realloc_(L, NULL, tag, (s))
 *  
 *  nsize 为 0 表示释放内存
 *  #define luaM_freearray(L, b, n) luaM_realloc_(L, (b), (n)*sizeof(*(b)), 0)
 *  
 *  内部调用 g->frealloc 来重新分配内存
 */
void *luaM_realloc_ (lua_State *L, void *block, size_t osize, size_t nsize) {
  void *newblock;
  global_State *g = G(L);
  size_t realosize = (block) ? osize : 0;
  lua_assert((realosize == 0) == (block == NULL));
#if defined(HARDMEMTESTS)
  if (nsize > realosize && g->gcrunning)
    luaC_fullgc(L, 1);  /* force a GC whenever possible */
#endif
  newblock = (*g->frealloc)(g->ud, block, osize, nsize);
  if (newblock == NULL && nsize > 0) {
    api_check( nsize > realosize,
                 "realloc cannot fail when shrinking a block");
    luaC_fullgc(L, 1);  /* try to free some memory... */
    newblock = (*g->frealloc)(g->ud, block, osize, nsize);  /* try again */
    if (newblock == NULL)
      luaD_throw(L, LUA_ERRMEM);
  }
  lua_assert((nsize == 0) == (newblock == NULL));
  g->GCdebt = (g->GCdebt + nsize) - realosize;
  return newblock;
}

