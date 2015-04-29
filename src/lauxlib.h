/*
** $Id: lauxlib.h,v 1.128 2014/10/29 16:11:17 roberto Exp $
** Auxiliary functions for building Lua libraries
** See Copyright Notice in lua.h
*/


#ifndef lauxlib_h
#define lauxlib_h


#include <stddef.h>
#include <stdio.h>

#include "lua.h"



/* extra error code for 'luaL_load' */
#define LUA_ERRFILE     (LUA_ERRERR+1)


/**
 * lua函数 与 C函数对应, lua math库都是直接调用C函数实现的
 */
typedef struct luaL_Reg {
  const char *name;
  lua_CFunction func;
} luaL_Reg;


#define LUAL_NUMSIZES	(sizeof(lua_Integer)*16 + sizeof(lua_Number))

LUALIB_API void (luaL_checkversion_) (lua_State *L, lua_Number ver, size_t sz);
#define luaL_checkversion(L)  \
	  luaL_checkversion_(L, LUA_VERSION_NUM, LUAL_NUMSIZES)

LUALIB_API int (luaL_getmetafield) (lua_State *L, int obj, const char *e);
LUALIB_API int (luaL_callmeta) (lua_State *L, int obj, const char *e);
LUALIB_API const char *(luaL_tolstring) (lua_State *L, int idx, size_t *len);
LUALIB_API int (luaL_argerror) (lua_State *L, int arg, const char *extramsg);
/** 
 * 返回栈arg位置字符串首地址，由 l 返回元素转成TString后的字符串长度, 
 * 元素可以是TString或Number类型 
 */
LUALIB_API const char *(luaL_checklstring) (lua_State *L, int arg,
                                                          size_t *l);
/**
 * 实现可选字符串参数;
 * arg: index
 * def: 默认值
 * len: 返回字符串长度
 * 返回值: 字符串首地址
 */
LUALIB_API const char *(luaL_optlstring) (lua_State *L, int arg,
                                          const char *def, size_t *l);
/**
 * 检测参数是否是number类型, arg为参数的栈位置,
 * 不是会报错
 */
LUALIB_API lua_Number (luaL_checknumber) (lua_State *L, int arg);
/**
 * 获取函数的可选参数, 默认值是def, 如函数 math.atan
 */
LUALIB_API lua_Number (luaL_optnumber) (lua_State *L, int arg, lua_Number def);

/**
 * 检测栈arg位置的元素是否是integer类型, arg为参数的栈位置
 * 返回该元素.
 */
LUALIB_API lua_Integer (luaL_checkinteger) (lua_State *L, int arg);

/**
 * 可选参数.
 * 若栈arg位置有元素，则返回该元素，否则返回默认值 def.
 */
LUALIB_API lua_Integer (luaL_optinteger) (lua_State *L, int arg,
                                          lua_Integer def);

/**
 * 检测栈空间是否足够，不够话会自动增长，若需求太多的话可能报会stack overflow错
 * 
 * space: 用户需求的栈空间大小
 * msg: 错误信息
 */
LUALIB_API void (luaL_checkstack) (lua_State *L, int sz, const char *msg);
/**
 * 检查位置 arg 元素类型是否是 t 类型, 不是就报错
 * arg : 栈位置
 * t   : 类型
 */
LUALIB_API void (luaL_checktype) (lua_State *L, int arg, int t);
LUALIB_API void (luaL_checkany) (lua_State *L, int arg);

LUALIB_API int   (luaL_newmetatable) (lua_State *L, const char *tname);
LUALIB_API void  (luaL_setmetatable) (lua_State *L, const char *tname);
LUALIB_API void *(luaL_testudata) (lua_State *L, int ud, const char *tname);
LUALIB_API void *(luaL_checkudata) (lua_State *L, int ud, const char *tname);

LUALIB_API void (luaL_where) (lua_State *L, int lvl);
LUALIB_API int (luaL_error) (lua_State *L, const char *fmt, ...);

LUALIB_API int (luaL_checkoption) (lua_State *L, int arg, const char *def,
                                   const char *const lst[]);

LUALIB_API int (luaL_fileresult) (lua_State *L, int stat, const char *fname);
LUALIB_API int (luaL_execresult) (lua_State *L, int stat);

/* pre-defined references */
#define LUA_NOREF       (-2)
#define LUA_REFNIL      (-1)

LUALIB_API int (luaL_ref) (lua_State *L, int t);
LUALIB_API void (luaL_unref) (lua_State *L, int t, int ref);

LUALIB_API int (luaL_loadfilex) (lua_State *L, const char *filename,
                                               const char *mode);

#define luaL_loadfile(L,f)	luaL_loadfilex(L,f,NULL)

LUALIB_API int (luaL_loadbufferx) (lua_State *L, const char *buff, size_t sz,
                                   const char *name, const char *mode);
LUALIB_API int (luaL_loadstring) (lua_State *L, const char *s);

LUALIB_API lua_State *(luaL_newstate) (void);

LUALIB_API lua_Integer (luaL_len) (lua_State *L, int idx);

LUALIB_API const char *(luaL_gsub) (lua_State *L, const char *s, const char *p,
                                                  const char *r);

LUALIB_API void (luaL_setfuncs) (lua_State *L, const luaL_Reg *l, int nup);

LUALIB_API int (luaL_getsubtable) (lua_State *L, int idx, const char *fname);

LUALIB_API void (luaL_traceback) (lua_State *L, lua_State *L1,
                                  const char *msg, int level);

LUALIB_API void (luaL_requiref) (lua_State *L, const char *modname,
                                 lua_CFunction openf, int glb);

/*
** ===============================================================
** some useful macros
** ===============================================================
*/


#define luaL_newlibtable(L,l)	\
  lua_createtable(L, 0, sizeof(l)/sizeof((l)[0]) - 1)

/**
 * 依次进行lua版本检查, 创建 table, 设置函数
 */
#define luaL_newlib(L,l)  \
  (luaL_checkversion(L), luaL_newlibtable(L,l), luaL_setfuncs(L,l,0))

/**
 * 检验参数, extramsg 为错误信息, cond 为true则为true, 否则用
 * luaL_argerror检验arg
 */
#define luaL_argcheck(L, cond,arg,extramsg)	\
		((void)((cond) || luaL_argerror(L, (arg), (extramsg))))
#define luaL_checkstring(L,n)	(luaL_checklstring(L, (n), NULL))
#define luaL_optstring(L,n,d)	(luaL_optlstring(L, (n), (d), NULL))

#define luaL_typename(L,i)	lua_typename(L, lua_type(L,(i)))

#define luaL_dofile(L, fn) \
	(luaL_loadfile(L, fn) || lua_pcall(L, 0, LUA_MULTRET, 0))

#define luaL_dostring(L, s) \
	(luaL_loadstring(L, s) || lua_pcall(L, 0, LUA_MULTRET, 0))

#define luaL_getmetatable(L,n)	(lua_getfield(L, LUA_REGISTRYINDEX, (n)))

/**
 * 可选参数的实现.
 * f: 函数指针，f(L, n) 为栈上有值时候的返回值, 比如 f 可以是 luaL_checkinteger
 * d: 栈上无值时候的返回该默认值
 */
#define luaL_opt(L,f,n,d)	(lua_isnoneornil(L,(n)) ? (d) : f(L,(n)))

#define luaL_loadbuffer(L,s,sz,n)	luaL_loadbufferx(L,s,sz,n,NULL)


/*
** {======================================================
** Generic Buffer manipulation
** =======================================================
*/

typedef struct luaL_Buffer {
  char *b;  /* buffer address */
  size_t size;  /* buffer size */
  size_t n;  /* number of characters in buffer */
  lua_State *L;
  char initb[LUAL_BUFFERSIZE];  /* initial buffer */
} luaL_Buffer;


/* 往buffer中添加一个字符 */
#define luaL_addchar(B,c) \
  ((void)((B)->n < (B)->size || luaL_prepbuffsize((B), 1)), \
   ((B)->b[(B)->n++] = (c)))
/* 设置buffer中可用字符数数量 */
#define luaL_addsize(B,s)	((B)->n += (s))

/**
 * 初始化 buffer, 初始化buffer各个参数
 */
LUALIB_API void (luaL_buffinit) (lua_State *L, luaL_Buffer *B);
/*
** returns a pointer to a free area with at least 'sz' bytes
*/
/*
 * 原buffer空间不够的话会重新分配一片Udata的数据区域给buffer
 * 返回的是buffer内的有效地址的首地址
 */
LUALIB_API char *(luaL_prepbuffsize) (luaL_Buffer *B, size_t sz);
/**
 * 向buffer内写入一个字符串
 * s: 字符串首地址
 * l: 字符串长度
 */
LUALIB_API void (luaL_addlstring) (luaL_Buffer *B, const char *s, size_t l);
/* 写入 '\0' 结尾的字符串 */
LUALIB_API void (luaL_addstring) (luaL_Buffer *B, const char *s);
/**
 * 将对象转为字符串放入buffer中, lua_State *从buffer中获得
 */
LUALIB_API void (luaL_addvalue) (luaL_Buffer *B);
/**
 * 将buffer中的所有字符以字符串形式压入栈中
 */
LUALIB_API void (luaL_pushresult) (luaL_Buffer *B);
/**
 * 同上，不过可用字符数量要先加 sz 
 * sz: 新增的可用字符数量
 */
LUALIB_API void (luaL_pushresultsize) (luaL_Buffer *B, size_t sz);

 /** 
  * 分配大小至少为 sz 的 buffer, 返回buffer内部的可用数据区域首地址 
  */
LUALIB_API char *(luaL_buffinitsize) (lua_State *L, luaL_Buffer *B, size_t sz);

#define luaL_prepbuffer(B)	luaL_prepbuffsize(B, LUAL_BUFFERSIZE)

/* }====================================================== */



/*
** {======================================================
** File handles for IO library
** =======================================================
*/

/*
** A file handle is a userdata with metatable 'LUA_FILEHANDLE' and
** initial structure 'luaL_Stream' (it may contain other fields
** after that initial structure).
*/

#define LUA_FILEHANDLE          "FILE*"


typedef struct luaL_Stream {
  FILE *f;  /* stream (NULL for incompletely created streams) */
  lua_CFunction closef;  /* to close stream (NULL for closed streams) */
} luaL_Stream;

/* }====================================================== */



/* compatibility with old module system */
#if defined(LUA_COMPAT_MODULE)

LUALIB_API void (luaL_pushmodule) (lua_State *L, const char *modname,
                                   int sizehint);
LUALIB_API void (luaL_openlib) (lua_State *L, const char *libname,
                                const luaL_Reg *l, int nup);

#define luaL_register(L,n,l)	(luaL_openlib(L,(n),(l),0))

#endif


/*
** {==================================================================
** "Abstraction Layer" for basic report of messages and errors
** ===================================================================
*/

/* print a string */
#if !defined(lua_writestring)
#define lua_writestring(s,l)   fwrite((s), sizeof(char), (l), stdout)
#endif

/* print a newline and flush the output */
#if !defined(lua_writeline)
#define lua_writeline()        (lua_writestring("\n", 1), fflush(stdout))
#endif

/* print an error message */
#if !defined(lua_writestringerror)
#define lua_writestringerror(s,p) \
        (fprintf(stderr, (s), (p)), fflush(stderr))
#endif

/* }================================================================== */


/*
** {============================================================
** Compatibility with deprecated conversions
** =============================================================
*/
#if defined(LUA_COMPAT_APIINTCASTS)

#define luaL_checkunsigned(L,a)	((lua_Unsigned)luaL_checkinteger(L,a))
#define luaL_optunsigned(L,a,d)	\
	((lua_Unsigned)luaL_optinteger(L,a,(lua_Integer)(d)))

#define luaL_checkint(L,n)	((int)luaL_checkinteger(L, (n)))
#define luaL_optint(L,n,d)	((int)luaL_optinteger(L, (n), (d)))

#define luaL_checklong(L,n)	((long)luaL_checkinteger(L, (n)))
#define luaL_optlong(L,n,d)	((long)luaL_optinteger(L, (n), (d)))

#endif
/* }============================================================ */



#endif


