/*
** $Id: ltm.h,v 2.21 2014/10/25 11:50:46 roberto Exp $
** Tag methods
** See Copyright Notice in lua.h
*/

#ifndef ltm_h
#define ltm_h


#include "lobject.h"


/*
* WARNING: if you change the order of this enumeration,
* grep "ORDER TM" and "ORDER OP"
*/
typedef enum {
  TM_INDEX,
  TM_NEWINDEX,
  TM_GC,
  TM_MODE,
  TM_LEN,
  TM_EQ,  /* last tag method with fast access */
  TM_ADD,
  TM_SUB,
  TM_MUL,
  TM_MOD,
  TM_POW,
  TM_DIV,
  TM_IDIV,
  TM_BAND,
  TM_BOR,
  TM_BXOR,
  TM_SHL,
  TM_SHR,
  TM_UNM,
  TM_BNOT,
  TM_LT,
  TM_LE,
  TM_CONCAT,
  TM_CALL,
  TM_N		/* number of elements in the enum */
} TMS;



/**
 * 利用table 的 flags 字段 cache 没有的元方法来快速判断是否需要调用函数
 * 还是直接返回 NULL
 * g: global_State
 * et: 元表
 * e: 元方法序列号
 * 
 * 当 settable 且没有触发元方法时将会使 flags 重置为 0
 */
#define gfasttm(g,et,e) ((et) == NULL ? NULL : \
  ((et)->flags & (1u<<(e))) ? NULL : luaT_gettm(et, e, (g)->tmname[e]))

#define fasttm(l,et,e)	gfasttm(G(l), et, e)

/* 返回 tag x 对应的类型名 */
#define ttypename(x)	luaT_typenames_[(x) + 1]
/* 返回对象 x 的基本类型名 */
#define objtypename(x)	ttypename(ttnov(x))

LUAI_DDEC const char *const luaT_typenames_[LUA_TOTALTAGS];


LUAI_FUNC const TValue *luaT_gettm (Table *events, TMS event, TString *ename);
/**
 * 获取对象的元方法
 * 
 * o: 对象
 * event: 元方法类型
 */
LUAI_FUNC const TValue *luaT_gettmbyobj (lua_State *L, const TValue *o,
                                                       TMS event);
/* 初始化元方法名数组 */
LUAI_FUNC void luaT_init (lua_State *L);

LUAI_FUNC void luaT_callTM (lua_State *L, const TValue *f, const TValue *p1,
                            const TValue *p2, TValue *p3, int hasres);
LUAI_FUNC int luaT_callbinTM (lua_State *L, const TValue *p1, const TValue *p2,
                              StkId res, TMS event);
LUAI_FUNC void luaT_trybinTM (lua_State *L, const TValue *p1, const TValue *p2,
                              StkId res, TMS event);
LUAI_FUNC int luaT_callorderTM (lua_State *L, const TValue *p1,
                                const TValue *p2, TMS event);



#endif
