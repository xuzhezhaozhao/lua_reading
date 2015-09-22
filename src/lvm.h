/*
** $Id: lvm.h,v 2.34 2014/08/01 17:24:02 roberto Exp $
** Lua virtual machine
** See Copyright Notice in lua.h
*/

#ifndef lvm_h
#define lvm_h


#include "ldo.h"
#include "lobject.h"
#include "ltm.h"


#if !defined(LUA_NOCVTN2S)
#define cvt2str(o)	ttisnumber(o)
#else
#define cvt2str(o)	0	/* no conversion from numbers to strings */
#endif


#if !defined(LUA_NOCVTS2N)
#define cvt2num(o)	ttisstring(o)
#else
#define cvt2num(o)	0	/* no conversion from strings to numbers */
#endif


/* 将 o 转为浮点数，结果存在 *n 中, 成功返回1， 失败返回0 */
#define tonumber(o,n) \
	(ttisfloat(o) ? (*(n) = fltvalue(o), 1) : luaV_tonumber_(o,n))

/**
 * 将对象 o 转为 lua_Integer 类型，值保存在 *i 中，成功返回 true,
 * 处理浮点数可以选择舍入方式, 当前设置的是只接受整数，不接受浮点数, 
 * 参考函数 luaV_tointeger, 在 lvm.c 中定义
 */
#define tointeger(o,i) \
	(ttisinteger(o) ? (*(i) = ivalue(o), 1) : luaV_tointeger_(o,i))

/**
 * 先将 v1, v2 转为 unsigned, 计算结果再转为 signed
 * op: 运算符
 * v1: 第一个操作数
 * v2: 第二个操作数
 * TODO
 */
#define intop(op,v1,v2) l_castU2S(l_castS2U(v1) op l_castS2U(v2))

/**
 * 比较两个对象是否相等，不使用元方法
 */
#define luaV_rawequalobj(t1,t2)		luaV_equalobj(NULL,t1,t2)


LUAI_FUNC int luaV_equalobj (lua_State *L, const TValue *t1, const TValue *t2);
LUAI_FUNC int luaV_lessthan (lua_State *L, const TValue *l, const TValue *r);
LUAI_FUNC int luaV_lessequal (lua_State *L, const TValue *l, const TValue *r);
LUAI_FUNC int luaV_tonumber_ (const TValue *obj, lua_Number *n);
LUAI_FUNC int luaV_tointeger_ (const TValue *obj, lua_Integer *p);
/*
** Main function for table access (invoking metamethods if needed).
** Compute 'val = t[key]'
*/
LUAI_FUNC void luaV_gettable (lua_State *L, const TValue *t, TValue *key,
                                            StkId val);
/*
** Main function for table assignment (invoking metamethods if needed).
** Compute 't[key] = val'
*/
LUAI_FUNC void luaV_settable (lua_State *L, const TValue *t, TValue *key,
                                            StkId val);
LUAI_FUNC void luaV_finishOp (lua_State *L);
LUAI_FUNC void luaV_execute (lua_State *L);
/*
** Main operation for concatenation: concat 'total' values in the stack,
** from 'L->top - total' up to 'L->top - 1'.
*/
/**
 * 在 lua_concat (lapi.h) 中被调用, 若不是数字或字符串，
 * 将使用 __concat 元方法
 */
LUAI_FUNC void luaV_concat (lua_State *L, int total);
/* 整数除法运算，返回 x // y */
LUAI_FUNC lua_Integer luaV_div (lua_State *L, lua_Integer x, lua_Integer y);
/* 取模运算实现，返回 x % y */
LUAI_FUNC lua_Integer luaV_mod (lua_State *L, lua_Integer x, lua_Integer y);
/* 移位运算，y > 0, 左移; y < 0, 右移 */
LUAI_FUNC lua_Integer luaV_shiftl (lua_Integer x, lua_Integer y);
/*
** Main operation 'ra' = #rb'.
*/
/* 有元表方法先会调用元表方法 __len */
LUAI_FUNC void luaV_objlen (lua_State *L, StkId ra, const TValue *rb);

#endif
