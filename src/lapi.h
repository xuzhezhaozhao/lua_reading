/*
** $Id: lapi.h,v 2.8 2014/07/15 21:26:50 roberto Exp $
** Auxiliary functions from Lua API
** See Copyright Notice in lua.h
*/

#ifndef lapi_h
#define lapi_h


#include "llimits.h"
#include "lstate.h"

/* 栈顶指针增加1 */
#define api_incr_top(L)   {L->top++; api_check(L->top <= L->ci->top, \
				"stack overflow");}

#define adjustresults(L,nres) \
    { if ((nres) == LUA_MULTRET && L->ci->top < L->top) L->ci->top = L->top; }


/* 检测栈中元素数量是否足够满足要求, 当前函数调用栈中至少要有 n 个元素 */
#define api_checknelems(L,n)	api_check((n) < (L->top - L->ci->func), \
				  "not enough elements in the stack")


#endif
