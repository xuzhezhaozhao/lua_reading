/*
** $Id: loslib.c,v 1.54 2014/12/26 14:46:07 roberto Exp $
** Standard Operating System library
** See Copyright Notice in lua.h
*/

#define loslib_c
#define LUA_LIB

#include "lprefix.h"


#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"


#if !defined(LUA_STRFTIMEOPTIONS)	/* { */
/*
** list of valid conversion specifiers for the 'strftime' function
*/

#if defined(LUA_USE_C89)
#define LUA_STRFTIMEOPTIONS	{ "aAbBcdHIjmMpSUwWxXyYz%", "" }
#else  /* C99 specification */
#define LUA_STRFTIMEOPTIONS \
	{ "aAbBcCdDeFgGhHIjmMnprRStTuUVwWxXyYzZ%", "", \
	  "E", "cCxXyY",  \
	  "O", "deHImMSuUVwWy" }
#endif

#endif					/* } */



#if !defined(l_time_t)		/* { */
/*
** type to represent time_t in Lua
*/
#define l_timet			lua_Integer
#define l_pushtime(L,t)		lua_pushinteger(L,(lua_Integer)(t))
#define l_checktime(L,a)	((time_t)luaL_checkinteger(L,a))

#endif				/* } */



#if !defined(lua_tmpnam)	/* { */
/*
** By default, Lua uses tmpnam except when POSIX is available, where it
** uses mkstemp.
*/

#if defined(LUA_USE_POSIX)	/* { */

#include <unistd.h>

#define LUA_TMPNAMBUFSIZE	32

#if !defined(LUA_TMPNAMTEMPLATE)
#define LUA_TMPNAMTEMPLATE	"/tmp/lua_XXXXXX"
#endif

#define lua_tmpnam(b,e) { \
        strcpy(b, LUA_TMPNAMTEMPLATE); \
        e = mkstemp(b); \
        if (e != -1) close(e); \
        e = (e == -1); }

#else				/* }{ */

/* ISO C definitions */
#define LUA_TMPNAMBUFSIZE	L_tmpnam
#define lua_tmpnam(b,e)		{ e = (tmpnam(b) == NULL); }

#endif				/* } */

#endif				/* } */



#if !defined(l_gmtime)		/* { */
/*
** By default, Lua uses gmtime/localtime, except when POSIX is available,
** where it uses gmtime_r/localtime_r
*/

#if defined(LUA_USE_POSIX)	/* { */

#define l_gmtime(t,r)		gmtime_r(t,r)
#define l_localtime(t,r)	localtime_r(t,r)

#else				/* }{ */

/* ISO C definitions */
#define l_gmtime(t,r)		((void)(r)->tm_sec, gmtime(t))
#define l_localtime(t,r)  	((void)(r)->tm_sec, localtime(t))

#endif				/* } */

#endif				/* } */


/*
 * This function is equivalent to the ISO C function system. It 
 * passes command to be executed by an operating system shell. 
 * Its first result is true if the command terminated successfully,
 * or nil otherwise. After this first result the function returns 
 * a string plus a number, as follows:
 *
 *   "exit": the command terminated normally; the following number
 *      is the exit status of the command.
 *   "signal": the command was terminated by a signal; the following 
 *      number is the signal that terminated the command.
 *      
 * When called without a command, os.execute returns a boolean that 
 * is true if a shell is available.
 */
/**
 * 返回值是表示 栈顶返回值的数目
 */
static int os_execute (lua_State *L) {
  const char *cmd = luaL_optstring(L, 1, NULL);
  int stat = system(cmd);
  if (cmd != NULL)
    return luaL_execresult(L, stat);
  else {
    lua_pushboolean(L, stat);  /* true if there is a shell */
    return 1;
  }
}

/*
 * Deletes the file (or empty directory, on POSIX systems) with the given
 * name. If this function fails, it returns nil, plus a string describing
 * the error and the error code.
*/
/**
 * 文件名字符串保存在栈顶
 */
static int os_remove (lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  return luaL_fileresult(L, remove(filename) == 0, filename);
}

/*
 * Renames file or directory named oldname to newname. If this 
 * function fails, it returns nil, plus a string describing the
 * error and the error code.
*/
/**
 * 两个参数保存在栈顶
 */
static int os_rename (lua_State *L) {
  const char *fromname = luaL_checkstring(L, 1);
  const char *toname = luaL_checkstring(L, 2);
  return luaL_fileresult(L, rename(fromname, toname) == 0, NULL);
}

/*
 * Returns a string with a file name that can be used for a temporary
 * file. The file must be explicitly opened before its use and 
 * explicitly removed when no longer needed.
 *
 * On POSIX systems, this function also creates a file with that 
 * name, to avoid security risks. (Someone else might create the 
 * file with wrong permissions in the time between getting the 
 * name and creating the file.) You still have to open the file to
 * use it and to remove it (even if you do not use it).
 *
 * When possible, you may prefer to use io.tmpfile, which 
 * automatically removes the file when the program ends.
*/
static int os_tmpname (lua_State *L) {
  char buff[LUA_TMPNAMBUFSIZE];
  int err;
  lua_tmpnam(buff, err);
  if (err)
    return luaL_error(L, "unable to generate a unique filename");
  lua_pushstring(L, buff);
  return 1;
}

/*
 * Returns the value of the process environment variable varname,
 * or nil if the variable is not defined.
 */
static int os_getenv (lua_State *L) {
  lua_pushstring(L, getenv(luaL_checkstring(L, 1)));  /* if NULL push nil */
  return 1;
}

/*
 * Returns an approximation of the amount in seconds of CPU time
 * used by the program.
 */
static int os_clock (lua_State *L) {
  lua_pushnumber(L, ((lua_Number)clock())/(lua_Number)CLOCKS_PER_SEC);
  return 1;
}


/*
** {======================================================
** Time/Date operations
** { year=%Y, month=%m, day=%d, hour=%H, min=%M, sec=%S,
**   wday=%w+1, yday=%j, isdst=? }
** =======================================================
*/

/**
 * t[key] = value, t 为栈顶元素，可能使用元方法 
 */
static void setfield (lua_State *L, const char *key, int value) {
  lua_pushinteger(L, value);
  lua_setfield(L, -2, key);
}

/**
 * 同上，t[key] = bool(value)
 */
static void setboolfield (lua_State *L, const char *key, int value) {
  if (value < 0)  /* undefined? */
    return;  /* does not set field */
  lua_pushboolean(L, value);
  lua_setfield(L, -2, key);
}

/**
 * 获取 t[key], 不存在则返回 -1
 */
static int getboolfield (lua_State *L, const char *key) {
  int res;
  res = (lua_getfield(L, -1, key) == LUA_TNIL) ? -1 : lua_toboolean(L, -1);
  lua_pop(L, 1);
  return res;
}

/**
 * 获取 t[k], t为栈顶元素, t[k] 为整数直接返回,
 * 若 t[k] 不是整数类型, 若 d >= 0, 返回d, 若 d < 0, 报错
 */
static int getfield (lua_State *L, const char *key, int d) {
  int res, isnum;
  /* push t[k] to stack */
  lua_getfield(L, -1, key);
  res = (int)lua_tointegerx(L, -1, &isnum);
  if (!isnum) {
    if (d < 0)
      return luaL_error(L, "field '%s' missing in date table", key);
    res = d;
  }
  lua_pop(L, 1);
  return res;
}


static const char *checkoption (lua_State *L, const char *conv, char *buff) {
  static const char *const options[] = LUA_STRFTIMEOPTIONS;
  unsigned int i;
  for (i = 0; i < sizeof(options)/sizeof(options[0]); i += 2) {
    if (*conv != '\0' && strchr(options[i], *conv) != NULL) {
      buff[1] = *conv;
      if (*options[i + 1] == '\0') {  /* one-char conversion specifier? */
        buff[2] = '\0';  /* end buffer */
        return conv + 1;
      }
      else if (*(conv + 1) != '\0' &&
               strchr(options[i + 1], *(conv + 1)) != NULL) {
        buff[2] = *(conv + 1);  /* valid two-char conversion specifier */
        buff[3] = '\0';  /* end buffer */
        return conv + 2;
      }
    }
  }
  luaL_argerror(L, 1,
    lua_pushfstring(L, "invalid conversion specifier '%%%s'", conv));
  return conv;  /* to avoid warnings */
}


static int os_date (lua_State *L) {
  const char *s = luaL_optstring(L, 1, "%c");
  time_t t = luaL_opt(L, l_checktime, 2, time(NULL));
  struct tm tmr, *stm;
  if (*s == '!') {  /* UTC? */
    stm = l_gmtime(&t, &tmr);
    s++;  /* skip '!' */
  }
  else
    stm = l_localtime(&t, &tmr);
  if (stm == NULL)  /* invalid date? */
    lua_pushnil(L);
  else if (strcmp(s, "*t") == 0) {
    lua_createtable(L, 0, 9);  /* 9 = number of fields */
    setfield(L, "sec", stm->tm_sec);
    setfield(L, "min", stm->tm_min);
    setfield(L, "hour", stm->tm_hour);
    setfield(L, "day", stm->tm_mday);
    setfield(L, "month", stm->tm_mon+1);
    setfield(L, "year", stm->tm_year+1900);
    setfield(L, "wday", stm->tm_wday+1);
    setfield(L, "yday", stm->tm_yday+1);
    setboolfield(L, "isdst", stm->tm_isdst);
  }
  else {
    char cc[4];
    luaL_Buffer b;
    cc[0] = '%';
    luaL_buffinit(L, &b);
    while (*s) {
      if (*s != '%')  /* no conversion specifier? */
        luaL_addchar(&b, *s++);
      else {
        size_t reslen;
        char buff[200];  /* should be big enough for any conversion result */
        s = checkoption(L, s + 1, cc);
        reslen = strftime(buff, sizeof(buff), cc, stm);
        luaL_addlstring(&b, buff, reslen);
      }
    }
    luaL_pushresult(&b);
  }
  return 1;
}


static int os_time (lua_State *L) {
  time_t t;
  if (lua_isnoneornil(L, 1))  /* called without args? */
    t = time(NULL);  /* get current time */
  else {
    struct tm ts;
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_settop(L, 1);  /* make sure table is at the top */
    ts.tm_sec = getfield(L, "sec", 0);
    ts.tm_min = getfield(L, "min", 0);
    ts.tm_hour = getfield(L, "hour", 12);
    ts.tm_mday = getfield(L, "day", -1);
    ts.tm_mon = getfield(L, "month", -1) - 1;
    ts.tm_year = getfield(L, "year", -1) - 1900;
    ts.tm_isdst = getboolfield(L, "isdst");
    t = mktime(&ts);
  }
  if (t != (time_t)(l_timet)t)
    luaL_error(L, "time result cannot be represented in this Lua instalation");
  else if (t == (time_t)(-1))
    lua_pushnil(L);
  else
    l_pushtime(L, t);
  return 1;
}


static int os_difftime (lua_State *L) {
  double res = difftime((l_checktime(L, 1)), (l_checktime(L, 2)));
  lua_pushnumber(L, (lua_Number)res);
  return 1;
}

/* }====================================================== */


static int os_setlocale (lua_State *L) {
  static const int cat[] = {LC_ALL, LC_COLLATE, LC_CTYPE, LC_MONETARY,
                      LC_NUMERIC, LC_TIME};
  static const char *const catnames[] = {"all", "collate", "ctype", "monetary",
     "numeric", "time", NULL};
  const char *l = luaL_optstring(L, 1, NULL);
  int op = luaL_checkoption(L, 2, "all", catnames);
  lua_pushstring(L, setlocale(cat[op], l));
  return 1;
}

/*
 * Calls the ISO C function exit to terminate the host program. 
 * If code is true, the returned status is EXIT_SUCCESS; if code
 * is false, the returned status is EXIT_FAILURE; if code is a 
 * number, the returned status is this number. The default value
 * for code is true.
 *
 * If the optional second argument close is true, closes the Lua
 * state before exiting.
 */
static int os_exit (lua_State *L) {
  int status;
  if (lua_isboolean(L, 1))
    status = (lua_toboolean(L, 1) ? EXIT_SUCCESS : EXIT_FAILURE);
  else
    status = (int)luaL_optinteger(L, 1, EXIT_SUCCESS);
  if (lua_toboolean(L, 2))
    lua_close(L);
  if (L) exit(status);  /* 'if' to avoid warnings for unreachable 'return' */
  return 0;
}


static const luaL_Reg syslib[] = {
  {"clock",     os_clock},
  {"date",      os_date},
  {"difftime",  os_difftime},
  {"execute",   os_execute},
  {"exit",      os_exit},
  {"getenv",    os_getenv},
  {"remove",    os_remove},
  {"rename",    os_rename},
  {"setlocale", os_setlocale},
  {"time",      os_time},
  {"tmpname",   os_tmpname},
  {NULL, NULL}
};

/* }====================================================== */



LUAMOD_API int luaopen_os (lua_State *L) {
  luaL_newlib(L, syslib);
  return 1;
}

