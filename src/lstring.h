/*
** $Id: lstring.h,v 1.56 2014/07/18 14:46:47 roberto Exp $
** String table (keep all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#ifndef lstring_h
#define lstring_h

#include "lgc.h"
#include "lobject.h"
#include "lstate.h"


/* l: 为C字符串长度, 这个宏计算对应的 Lua 字符串长度 */
#define sizelstring(l)  (sizeof(union UTString) + ((l) + 1) * sizeof(char))
#define sizestring(s)	sizelstring((s)->len)

#define sizeludata(l)	(sizeof(union UUdata) + (l))
#define sizeudata(u)	sizeludata((u)->len)

#define luaS_newliteral(L, s)	(luaS_newlstr(L, "" s, \
                                 (sizeof(s)/sizeof(char))-1))


/*
** test whether a string is a reserved word
*/
#define isreserved(s)	((s)->tt == LUA_TSHRSTR && (s)->extra > 0)


/*
** equality for short strings, which are always internalized
*/
#define eqshrstr(a,b)	check_exp((a)->tt == LUA_TSHRSTR, (a) == (b))


/**
 * 字符串 hash 值
 */
LUAI_FUNC unsigned int luaS_hash (const char *str, size_t l, unsigned int seed);
LUAI_FUNC int luaS_eqlngstr (TString *a, TString *b);
LUAI_FUNC void luaS_resize (lua_State *L, int newsize);
LUAI_FUNC void luaS_remove (lua_State *L, TString *ts);
/**
 * 分配一个大小为 s 的 Udata 数据块，返回Udata类型的数据区域指针
 */
LUAI_FUNC Udata *luaS_newudata (lua_State *L, size_t s);
/* new string (with explicit length), 会重复利用短字符串资源 */
LUAI_FUNC TString *luaS_newlstr (lua_State *L, const char *str, size_t l);
/**
 * 新建一个以'\0'结尾的字符串
 */
LUAI_FUNC TString *luaS_new (lua_State *L, const char *str);


#endif
