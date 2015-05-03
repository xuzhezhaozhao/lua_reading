/*
** $Id: lapi.c,v 2.244 2014/12/26 14:43:45 roberto Exp $
** Lua API
** See Copyright Notice in lua.h
*/

#define lapi_c
#define LUA_CORE

#include "lprefix.h"


#include <stdarg.h>
#include <string.h>

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lundump.h"
#include "lvm.h"

#include <stdio.h>

const char lua_ident[] =
  "$LuaVersion: " LUA_COPYRIGHT " $"
  "$LuaAuthors: " LUA_AUTHORS " $";


/* value at a non-valid index */
#define NONVALIDVALUE		cast(TValue *, luaO_nilobject)

/* corresponding test */
/* o 指向的对象是否有效，即 o 不是指向 nil 对象 */
#define isvalid(o)	((o) != luaO_nilobject)

/* test for pseudo index */
/* i 是一个负很大的数时为真 */
#define ispseudo(i)		((i) <= LUA_REGISTRYINDEX) 
/* test for upvalue */
#define isupvalue(i)		((i) < LUA_REGISTRYINDEX)

/* test for valid but not pseudo index */
#define isstackindex(i, o)	(isvalid(o) && !ispseudo(i))

/* 确保 o 指向的对象不是 nil */
#define api_checkvalidindex(o)  api_check(isvalid(o), "invalid index")

/* 确保 o 不是指向 nil, 且 i 不是 pseudo index */
#define api_checkstackindex(i, o)  \
	api_check(isstackindex(i, o), "index not in the stack")


/**
 * 若 idx 无效，返回 NONVALIDVALUE
 * 
 * idx 还有可能是 pseudo index，返回对应 upvalue 的地址
 */
static TValue *index2addr (lua_State *L, int idx) {
  CallInfo *ci = L->ci;
  if (idx > 0) {
    TValue *o = ci->func + idx;
    api_check(idx <= ci->top - (ci->func + 1), "unacceptable index");
    if (o >= L->top) return NONVALIDVALUE;
    else return o;
  }
  else if (!ispseudo(idx)) {  /* negative index */
    api_check(idx != 0 && -idx <= L->top - (ci->func + 1), "invalid index");
    return L->top + idx;
  }
  else if (idx == LUA_REGISTRYINDEX)
    return &G(L)->l_registry;
  else {  /* upvalues */
    idx = LUA_REGISTRYINDEX - idx;
    api_check(idx <= MAXUPVAL + 1, "upvalue index too large");
    if (ttislcf(ci->func))  /* light C function? */
      return NONVALIDVALUE;  /* it has no upvalues */
    else {
      CClosure *func = clCvalue(ci->func);
      return (idx <= func->nupvalues) ? &func->upvalue[idx-1] : NONVALIDVALUE;
    }
  }
}


/*
** to be called by 'lua_checkstack' in protected mode, to grow stack
** capturing memory errors
*/
/* ud: 用户请求的栈空间大小 */
static void growstack (lua_State *L, void *ud) {
  int size = *(int *)ud;
  luaD_growstack(L, size);
}


/* 下面的函数全是 C-API，在官方文档中可以查到其用法 */

/**
 * 检测栈空间是否足够，不够的话会自动增长，栈空间总量有限制
 *
 * n: 所需要的栈大小
 * 返回值: 0 足够，1 不够
 */
LUA_API int lua_checkstack (lua_State *L, int n) {
  int res;
  CallInfo *ci = L->ci;
  lua_lock(L);
  api_check(n >= 0, "negative 'n'");
  if (L->stack_last - L->top > n)  /* stack large enough? */
    res = 1;  /* yes; check is OK */
  else {  /* no; need to grow stack */
    int inuse = cast_int(L->top - L->stack) + EXTRA_STACK;
    if (inuse > LUAI_MAXSTACK - n)  /* can grow without overflow? */
      res = 0;  /* no */
    else  /* try to grow stack */
      res = (luaD_rawrunprotected(L, &growstack, &n) == LUA_OK);
  }
  if (res && ci->top < L->top + n)
    ci->top = L->top + n;  /* adjust frame top */
  lua_unlock(L);
  return res;
}

/*
 * Exchange values between different threads of the same state.
 * This function pops n values from the stack from, and pushes 
 * them onto the stack to.
 */
LUA_API void lua_xmove (lua_State *from, lua_State *to, int n) {
  int i;
  if (from == to) return;
  lua_lock(to);
  api_checknelems(from, n);
  api_check(G(from) == G(to), "moving among independent states");
  api_check(to->ci->top - to->top >= n, "not enough elements to move");
  from->top -= n;
  for (i = 0; i < n; i++) {
    setobj2s(to, to->top++, from->top + i);
  }
  lua_unlock(to);
}

/**
 * 设置 L->panic 为 panicf，并返回原来的值
 * 
 * 关于 panic 函数, 见 http://www.lua.org/manual/5.3/manual.html#4.6
 */
LUA_API lua_CFunction lua_atpanic (lua_State *L, lua_CFunction panicf) {
  lua_CFunction old;
  lua_lock(L);
  old = G(L)->panic;
  G(L)->panic = panicf;
  lua_unlock(L);
  return old;
}

/* lua 版本号 */
LUA_API const lua_Number *lua_version (lua_State *L) {
  static const lua_Number version = LUA_VERSION_NUM;
  if (L == NULL) return &version;
  else return G(L)->version;
}



/*
** basic stack manipulation
*/


/*
** convert an acceptable stack index into an absolute index
*/
/* 负数索引的处理, 还有 pseudo-indices (LUA_REGISTRYINDEX) */
LUA_API int lua_absindex (lua_State *L, int idx) {
  return (idx > 0 || ispseudo(idx))
         ? idx
         : cast_int(L->top - L->ci->func + idx);
}

/*
 * Returns the index of the top element in the stack. Because 
 * indices start at 1, this result is equal to the number of 
 * elements in the stack; in particular, 0 means an empty stack.
 */
/**
 * 返回当前当前函数调用栈上元素的个数
 */
LUA_API int lua_gettop (lua_State *L) {
  return cast_int(L->top - (L->ci->func + 1));
}

/*
 * Accepts any index, or 0, and sets the stack top to this 
 * index. If the new top is larger than the old one, then 
 * the new elements are filled with nil. If index is 0, then
 * all stack elements are removed.
 */
/**
 * 设置栈顶为 idx 位置，
 * idx > 0 时, idx是相对于当前函数调用，L->top = (func + 1) + idx,
 * func + 1 位置应该是表示函数参数的个数，
 * 并将原栈顶与新栈顶之间的元素设为 nil.
 *  
 * idx < 0 时, L->top += idx + 1
 */
LUA_API void lua_settop (lua_State *L, int idx) {
  StkId func = L->ci->func;
  lua_lock(L);
  if (idx >= 0) {
    api_check(idx <= L->stack_last - (func + 1), "new top too large");
    while (L->top < (func + 1) + idx)
      setnilvalue(L->top++);
    L->top = (func + 1) + idx;
  }
  else {
    api_check(-(idx+1) <= (L->top - (func + 1)), "invalid new top");
    L->top += idx+1;  /* 'subtract' index (index is negative) */
  }
  lua_unlock(L);
}


/*
** Reverse the stack segment from 'from' to 'to'
** (auxiliary to 'lua_rotate')
*/
static void reverse (lua_State *L, StkId from, StkId to) {
  for (; from < to; from++, to--) {
    TValue temp;
    setobj(L, &temp, from);
    setobjs2s(L, from, to);
    setobj2s(L, to, &temp);
  }
}


/*
** Let x = AB, where A is a prefix of length 'n'. Then,
** rotate x n == BA. But BA == (A^r . B^r)^r.
*/
/*
 * Rotates the stack elements from idx to the top n positions
 * in the direction of the top, for a positive n, or -n positions
 * in the direction of the bottom, for a negative n. The absolute
 * value of n must not be greater than the size of the slice being
 * rotated.
 */
/**
 * 旋转栈的指定部分
 * 
 * idx: 需要旋转的段起始位置, 末尾位置是栈末尾 top - 1 位置
 * n:  旋转点, 看下面的例子 
 * 
 * 例如 若栈数组 [idx ~ top-1] 为 [1, 2, 3, 4, 5, 6, 7]，当n为3或-4时， 
 * 旋转为结果 [5, 6, 7, 1, 2, 3, 4].
 * 
 * 使用的算法是3次reverse操作.
 */
LUA_API void lua_rotate (lua_State *L, int idx, int n) {
  StkId p, t, m;
  lua_lock(L);
  t = L->top - 1;  /* end of stack segment being rotated */
  p = index2addr(L, idx);  /* start of segment */
  api_checkstackindex(idx, p);
  api_check((n >= 0 ? n : -n) <= (t - p + 1), "invalid 'n'");
  m = (n >= 0 ? t - n : p - n - 1);  /* end of prefix */
  reverse(L, p, m);  /* reverse the prefix with length 'n' */
  reverse(L, m + 1, t);  /* reverse the suffix */
  reverse(L, p, t);  /* reverse the entire segment */
  lua_unlock(L);
}

/**
 * 复制栈元素
 * 
 * fromidx: 源栈元素位置
 * toidx: 目的栈元素位置
 * 
 * TODO function upvalue 的处理?
 */
LUA_API void lua_copy (lua_State *L, int fromidx, int toidx) {
  TValue *fr, *to;
  lua_lock(L);
  fr = index2addr(L, fromidx);
  to = index2addr(L, toidx);
  api_checkvalidindex(to);
  setobj(L, to, fr);
  if (isupvalue(toidx))  /* function upvalue? */
    luaC_barrier(L, clCvalue(L->ci->func), fr);
  /* LUA_REGISTRYINDEX does not need gc barrier
     (collector revisits it before finishing collection) */
  lua_unlock(L);
}

/*
/ * Pushes a copy of the element at the given index onto the stack.
 */
/* 将栈位置 idx 的对象复制到栈顶 */
LUA_API void lua_pushvalue (lua_State *L, int idx) {
  lua_lock(L);
  setobj2s(L, L->top, index2addr(L, idx));
  api_incr_top(L);
  lua_unlock(L);
}



/*
** access functions (stack -> C)
*/

/*
 * Returns the type of the value in the given valid index, 
 * or LUA_TNONE for a non-valid (but acceptable) index. The
 * types returned by lua_type are coded by the following 
 * constants defined in lua.h: LUA_TNIL, LUA_TNUMBER, 
 * LUA_TBOOLEAN, LUA_TSTRING, LUA_TTABLE, LUA_TFUNCTION, 
 * LUA_TUSERDATA, LUA_TTHREAD, and LUA_TLIGHTUSERDATA.
 */
/**
 * 获取 id 位置元素 类型 tag
 * nil 则返回 LUA_TNONE
 */
LUA_API int lua_type (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return (isvalid(o) ? ttnov(o) : LUA_TNONE);
}

/**
 * 返回tag t 对应的基本类型名 
 */
LUA_API const char *lua_typename (lua_State *L, int t) {
  UNUSED(L);
  api_check(LUA_TNONE <= t && t < LUA_NUMTAGS, "invalid tag");
  return ttypename(t);
}


/**
 * 判断栈位置 idx 元素是否是函数类型，包括 C 和C Closure 函数
 */
LUA_API int lua_iscfunction (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return (ttislcf(o) || (ttisCclosure(o)));
}


/*
 * Returns 1 if the value at the given index is an integer 
 * (that is, the value is a number and is represented as an 
 * integer), and 0 otherwise.
 */
/**
 * 是否是整数，不进行类型转换
 */
LUA_API int lua_isinteger (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return ttisinteger(o);
}


/*
 * Returns 1 if the value at the given index is a number or
 * a string convertible to a number, and 0 otherwise.
 */
/**
 * 是否是number类型，会进行类型转换
 */
LUA_API int lua_isnumber (lua_State *L, int idx) {
  lua_Number n;
  const TValue *o = index2addr(L, idx);
  return tonumber(o, &n);
}

/*
 * Returns 1 if the value at the given index is a string or a 
 * number (which is always convertible to a string), and 0 otherwise.
 */
/**
 * 是否是 string 或 number
 */
LUA_API int lua_isstring (lua_State *L, int idx) {
  const TValue *o = index2addr(L, idx);
  return (ttisstring(o) || cvt2str(o));
}


/**
 * 是否是 uerdata, 包括 light userdata 和 full userdata
 */
LUA_API int lua_isuserdata (lua_State *L, int idx) {
  const TValue *o = index2addr(L, idx);
  return (ttisfulluserdata(o) || ttislightuserdata(o));
}

/*
 * Returns 1 if the two values in indices index1 and index2 
 * are primitively equal (that is, without calling metamethods).
 * Otherwise returns 0. Also returns 0 if any of the indices are
 * not valid.
 */
/**
 * index1, index2 位置的两个对象是否相等, 不调用元方法
 */
LUA_API int lua_rawequal (lua_State *L, int index1, int index2) {
  StkId o1 = index2addr(L, index1);
  StkId o2 = index2addr(L, index2);
  return (isvalid(o1) && isvalid(o2)) ? luaV_rawequalobj(o1, o2) : 0;
}


/*
 * Performs an arithmetic or bitwise operation over the two values
 * (or one, in the case of negations) at the top of the stack, with
 * the value at the top being the second operand, pops these values,
 * and pushes the result of the operation. The function follows the
 * semantics of the corresponding Lua operator (that is, it may call
 * metamethods).

 * The value of op must be one of the following constants:
 *
 *    LUA_OPADD: performs addition (+)
 *    LUA_OPSUB: performs subtraction (-)
 *    LUA_OPMUL: performs multiplication (*)
 *    LUA_OPDIV: performs float division (/)
 *    LUA_OPIDIV: performs floor division (//)
 *    LUA_OPMOD: performs modulo (%)
 *    LUA_OPPOW: performs exponentiation (^)
 *    LUA_OPUNM: performs mathematical negation (unary -)
 *    LUA_OPBNOT: performs bitwise negation (~)
 *    LUA_OPBAND: performs bitwise and (&)
 *    LUA_OPBOR: performs bitwise or (|)
 *    LUA_OPBXOR: performs bitwise exclusive or (~)
 *    LUA_OPSHL: performs left shift (<<)
 *    LUA_OPSHR: performs right shift (>>)
 */

/**
 * 各种数学运算符如加减乘除、移位、与或非实现, 操作数存放在栈上, 不包含比较运算
 *
 * op: 运算符类型， lua.h 中定义
 * 
 * 最后的运算结果存放在栈顶
 */
LUA_API void lua_arith (lua_State *L, int op) {
  lua_lock(L);
  if (op != LUA_OPUNM && op != LUA_OPBNOT)
    api_checknelems(L, 2);  /* all other operations expect two operands */
  else {  /* for unary operations, add fake 2nd operand */
    api_checknelems(L, 1);
    setobjs2s(L, L->top, L->top - 1);
    L->top++;
  }
  /* first operand at top - 2, second at top - 1; result go to top - 2 */
  luaO_arith(L, op, L->top - 2, L->top - 1, L->top - 2);
  L->top--;  /* remove second operand */
  lua_unlock(L);
}


/*
 * Compares two Lua values. Returns 1 if the value at index index1
 * satisfies op when compared with the value at index index2, 
 * following the semantics of the corresponding Lua operator (that
 * is, it may call metamethods). Otherwise returns 0. Also returns
 * 0 if any of the indices is not valid.

 * The value of op must be one of the following constants:

 *    LUA_OPEQ: compares for equality (==)
 *    LUA_OPLT: compares for less than (<)
 *    LUA_OPLE: compares for less or equal (<=)
 */
/**
 * 比较运算符的实现，使用元方法
 */
LUA_API int lua_compare (lua_State *L, int index1, int index2, int op) {
  StkId o1, o2;
  int i = 0;
  lua_lock(L);  /* may call tag method */
  o1 = index2addr(L, index1);
  o2 = index2addr(L, index2);
  if (isvalid(o1) && isvalid(o2)) {
    switch (op) {
      case LUA_OPEQ: i = luaV_equalobj(L, o1, o2); break;
      case LUA_OPLT: i = luaV_lessthan(L, o1, o2); break;
      case LUA_OPLE: i = luaV_lessequal(L, o1, o2); break;
      default: api_check(0, "invalid option");
    }
  }
  lua_unlock(L);
  return i;
}


/*
 * Converts the zero-terminated string s to a number, pushes that
 * number into the stack, and returns the total size of the string,
 * that is, its length plus one. The conversion can result in an 
 * integer or a float, according to the lexical conventions of Lua
 * (see §3.1). The string may have leading and trailing spaces and 
 * a sign. If the string is not a valid numeral, returns 0 and pushes
 * nothing. (Note that the result can be used as a boolean, true if
 * the conversion succeeds.)
 */
/**
 * 成功返回字符串长度 + 1，失败返回0, 成功转换后的结果存放在栈顶
 */
LUA_API size_t lua_stringtonumber (lua_State *L, const char *s) {
  size_t sz = luaO_str2num(s, L->top);
  if (sz != 0)
    api_incr_top(L);
  return sz;
}


/*
 * Converts the Lua value at the given index to the C type 
 * lua_Number (see lua_Number). The Lua value must be a number
 * or a string convertible to a number (see §3.4.3); otherwise,
 * lua_tonumberx returns 0.

 * If isnum is not NULL, its referent is assigned a boolean value
 * that indicates whether the operation succeeded.
 */
/**
 * 将位置 idx 元素转成 number 类型, pisnum 返回是否转换成功
 *
 * 返回成功转换后的值
 */
LUA_API lua_Number lua_tonumberx (lua_State *L, int idx, int *pisnum) {
  lua_Number n;
  const TValue *o = index2addr(L, idx);
  int isnum = tonumber(o, &n);
  if (!isnum)
    n = 0;  /* call to 'tonumber' may change 'n' even if it fails */
  if (pisnum) *pisnum = isnum;
  return n;
}


/**
 * 栈idx位置元素若为 lua_Integer类型，则返回，若pisum 非 NULL， 置 *pisum 为1，
 * 否则置 pisum 为 0.
 */
LUA_API lua_Integer lua_tointegerx (lua_State *L, int idx, int *pisnum) {
  lua_Integer res;
  const TValue *o = index2addr(L, idx);
  int isnum = tointeger(o, &res);
  if (!isnum)
    res = 0;  /* call to 'tointeger' may change 'n' even if it fails */
  if (pisnum) *pisnum = isnum;
  return res;
}

/*
 * Converts the Lua value at the given index to a C boolean value
 * (0 or 1). Like all tests in Lua, lua_toboolean returns true for
 * any Lua value different from false and nil; otherwise it returns
 * false. (If you want to accept only actual boolean values, use 
 * lua_isboolean to test the value's type.)
 */
/**
 * 若 idx 位置不存在元素, 就是nil, 返回false，若存在元素且元素不是 flase，
 * 则返回true, 即使是整数0也返回true
 */
LUA_API int lua_toboolean (lua_State *L, int idx) {
  const TValue *o = index2addr(L, idx);
  return !l_isfalse(o);
}

/*
 * Converts the Lua value at the given index to a C string. If 
 * len is not NULL, it also sets *len with the string length. 
 * The Lua value must be a string or a number; otherwise, the 
 * function returns NULL. If the value is a number, then 
 * lua_tolstring also changes the actual value in the stack to
 * a string. (This change confuses lua_next when lua_tolstring
 * is applied to keys during a table traversal.)

 * lua_tolstring returns a fully aligned pointer to a string 
 * inside the Lua state. This string always has a zero ('\0') 
 * after its last character (as in C), but can contain other 
 * zeros in its body.

 * Because Lua has garbage collection, there is no guarantee that
 * the pointer returned by lua_tolstring will be valid after the
 * corresponding Lua value is removed from the stack.
 */
/**
 * 将idx位置元素转成字符串, 本身就是TString类型的话，直接获取内部的字符串指针，
 * 非字符串的话只能对Number类型进行转换，转换成功的话返回字符串首地址，转换后
 * 的字符串长度保存在len中（len 为 NULL 则不保存）. 
 * 若元素不是 TString 也不是 Number类型，则转换失败，返回 NULL.
 * 
 * 转换完成后，栈位置idx指向的元素就变成了转换后的字符串类型.
 */
LUA_API const char *lua_tolstring (lua_State *L, int idx, size_t *len) {
  StkId o = index2addr(L, idx);
  if (!ttisstring(o)) {
	  /* 类型不是string */
    if (!cvt2str(o)) {  /* not convertible? */
      if (len != NULL) *len = 0;
      return NULL;
    }
    lua_lock(L);  /* 'luaO_tostring' may create a new string */
    luaC_checkGC(L);
    o = index2addr(L, idx);  /* previous call may reallocate the stack */
    luaO_tostring(L, o);
    lua_unlock(L);
  }
  if (len != NULL) *len = tsvalue(o)->len;
  return svalue(o);
}


/*
 * Returns the raw "length" of the value at the given index: for 
 * strings, this is the string length; for tables, this is the 
 * result of the length operator ('#') with no metamethods; for 
 * userdata, this is the size of the block of memory allocated 
 * for the userdata; for other values, it is 0.
 */
/**
 * 返回元素长度, 只支持下面3种类型，其余类型都返回0
 * TString 类型: len
 * Udata 类型: len
 * Table 类型: luaH_getn()
 */
LUA_API size_t lua_rawlen (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  switch (ttnov(o)) {
    case LUA_TSTRING: return tsvalue(o)->len;
    case LUA_TUSERDATA: return uvalue(o)->len;
    case LUA_TTABLE: return luaH_getn(hvalue(o));
    default: return 0;
  }
}

/*
 * Converts a value at the given index to a C function. That
 * value must be a C function; otherwise, returns NULL.
 */
/**
 * 只支持 lua_CFunction 和 Cclosure 类型, 其余返回 NULL
 */
LUA_API lua_CFunction lua_tocfunction (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  if (ttislcf(o)) return fvalue(o);
  else if (ttisCclosure(o))
    return clCvalue(o)->f;
  else return NULL;  /* not a C function */
}

/*
 * If the value at the given index is a full userdata, returns 
 * its block address. If the value is a light userdata, returns
 * its pointer. Otherwise, returns NULL.
 */
/**
 * 获取 Udata 或 light userdata (就是一个指针) 的数据区域首地址
 * 若元素不是 Udata 类型返回 NULL
 */
LUA_API void *lua_touserdata (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  switch (ttnov(o)) {
    case LUA_TUSERDATA: return getudatamem(uvalue(o));
    case LUA_TLIGHTUSERDATA: return pvalue(o);
    default: return NULL;
  }
}

/*
 * Converts the value at the given index to a Lua thread (represented
 * as lua_State*). This value must be a thread; otherwise, the function
 * returns NULL.
 */
/**
 * 转为 lua_State 指针, 失败返回 NULL
 */
LUA_API lua_State *lua_tothread (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  return (!ttisthread(o)) ? NULL : thvalue(o);
}

/*
 * Converts the value at the given index to a generic C pointer 
 * (void*). The value can be a userdata, a table, a thread, or 
 * a function; otherwise, lua_topointer returns NULL. Different
 * objects will give different pointers. There is no way to convert
 * the pointer back to its original value.

 * Typically this function is used only for debug information.
 */
/**
 * 将元素转为指针, 可以处理的类型 为 Table, CFuntion, closure, Thread, 
 * user data, light userdata, 其余类型返回 NULL
 */
LUA_API const void *lua_topointer (lua_State *L, int idx) {
  StkId o = index2addr(L, idx);
  switch (ttype(o)) {
    case LUA_TTABLE: return hvalue(o);
    case LUA_TLCL: return clLvalue(o);
    case LUA_TCCL: return clCvalue(o);
    case LUA_TLCF: return cast(void *, cast(size_t, fvalue(o)));
    case LUA_TTHREAD: return thvalue(o);
    case LUA_TUSERDATA:
    case LUA_TLIGHTUSERDATA:
      return lua_touserdata(L, idx);
    default: return NULL;
  }
}



/*
** push functions (C -> stack)
*/


/**
 * 往栈中压入一个 nil
 */
LUA_API void lua_pushnil (lua_State *L) {
  lua_lock(L);
  setnilvalue(L->top);
  api_incr_top(L);
  lua_unlock(L);
}


/**
 * 往栈中压入一个 Number
 */
LUA_API void lua_pushnumber (lua_State *L, lua_Number n) {
  lua_lock(L);
  setfltvalue(L->top, n);
  api_incr_top(L);
  lua_unlock(L);
}


/**
 * 往栈中压入一个 Integer 
 */
LUA_API void lua_pushinteger (lua_State *L, lua_Integer n) {
  lua_lock(L);
  setivalue(L->top, n);
  api_incr_top(L);
  lua_unlock(L);
}


/**
 * 往栈中压入一个 TString, 长度为 len 
 */
LUA_API const char *lua_pushlstring (lua_State *L, const char *s, size_t len) {
  TString *ts;
  lua_lock(L);
  luaC_checkGC(L);
  ts = luaS_newlstr(L, s, len);
  setsvalue2s(L, L->top, ts);
  api_incr_top(L);
  lua_unlock(L);
  return getstr(ts);
}

/*
 * Pushes the zero-terminated string pointed to by s onto the stack.
 * Lua makes (or reuses) an internal copy of the given string, so the
 * memory at s can be freed or reused immediately after the function
 * returns.

 * Returns a pointer to the internal copy of the string.

 * If s is NULL, pushes nil and returns NULL.
 */
/**
 * 往栈中压入一个 TString, s 为 NULL则压入 nil
 */
LUA_API const char *lua_pushstring (lua_State *L, const char *s) {
  if (s == NULL) {
    lua_pushnil(L);
    return NULL;
  }
  else {
    TString *ts;
    lua_lock(L);
    luaC_checkGC(L);
    ts = luaS_new(L, s);
    setsvalue2s(L, L->top, ts);
    api_incr_top(L);
    lua_unlock(L);
    return getstr(ts);
  }
}

/*
 * Equivalent to lua_pushfstring, except that it receives a va_list 
 * instead of a variable number of arguments.
 */
LUA_API const char *lua_pushvfstring (lua_State *L, const char *fmt,
                                      va_list argp) {
  const char *ret;
  lua_lock(L);
  luaC_checkGC(L);
  ret = luaO_pushvfstring(L, fmt, argp);
  lua_unlock(L);
  return ret;
}

/*
 * Pushes onto the stack a formatted string and returns a pointer
 * to this string. It is similar to the ISO C function sprintf, but
 * has some important differences:

 * You do not have to allocate space for the result: the result is 
 * a Lua string and Lua takes care of memory allocation (and 
 * deallocation, through garbage collection).
 * The conversion specifiers are quite restricted. There are no flags,
 * widths, or precisions. The conversion specifiers can only be '%%' 
 * (inserts the character '%'), '%s' (inserts a zero-terminated string,
 * with no size restrictions), '%f' (inserts a lua_Number), '%L' 
 * (inserts a lua_Integer), '%p' (inserts a pointer as a hexadecimal 
 * numeral), '%d' (inserts an int), '%c' (inserts an int as a one-byte
 * character), and '%U' (inserts a long int as a UTF-8 byte sequence).
 */
LUA_API const char *lua_pushfstring (lua_State *L, const char *fmt, ...) {
  const char *ret;
  va_list argp;
  lua_lock(L);
  luaC_checkGC(L);
  va_start(argp, fmt);
  ret = luaO_pushvfstring(L, fmt, argp);
  va_end(argp);
  lua_unlock(L);
  return ret;
}


/*
 * Pushes a new C closure onto the stack.

 * When a C function is created, it is possible to associate some 
 * values with it, thus creating a C closure (see §4.4); these values
 * are then accessible to the function whenever it is called. To 
 * associate values with a C function, first these values must be 
 * pushed onto the stack (when there are multiple values, the first
 * value is pushed first). Then lua_pushcclosure is called to create
 * and push the C function onto the stack, with the argument n telling
 * how many values will be associated with the function. 
 * lua_pushcclosure also pops these values from the stack.

 * The maximum value for n is 255.

 * When n is zero, this function creates a light C function, which is 
 * just a pointer to the C function. In that case, it never raises a 
 * memory error.
 */
/**
 * n 为 0时，直接压入 fn
 * n 不为 0 时，说明闭包有 n 个 upvalues
 */
LUA_API void lua_pushcclosure (lua_State *L, lua_CFunction fn, int n) {
  lua_lock(L);
  if (n == 0) {
    setfvalue(L->top, fn);
  }
  else {
    CClosure *cl;
    api_checknelems(L, n);
    api_check(n <= MAXUPVAL, "upvalue index too large");
    luaC_checkGC(L);
    cl = luaF_newCclosure(L, n);
    cl->f = fn;
    L->top -= n;
    while (n--) {
      setobj2n(L, &cl->upvalue[n], L->top + n);
      /* does not need barrier because closure is white */
    }
    setclCvalue(L, L->top, cl);
  }
  api_incr_top(L);
  lua_unlock(L);
}


/**
 * 压入 boolean
 */
LUA_API void lua_pushboolean (lua_State *L, int b) {
  lua_lock(L);
  setbvalue(L->top, (b != 0));  /* ensure that true is 1 */
  api_incr_top(L);
  lua_unlock(L);
}

/*
* Pushes a light userdata onto the stack.

* Userdata represent C values in Lua. A light userdata represents a 
* pointer, a void*. It is a value (like a number): you do not create
* it, it has no individual metatable, and it is not collected (as it
* was never created). A light userdata is equal to "any" light 
* userdata with the same C address.
*/
LUA_API void lua_pushlightuserdata (lua_State *L, void *p) {
  lua_lock(L);
  setpvalue(L->top, p);
  api_incr_top(L);
  lua_unlock(L);
}


/*
 * Pushes the thread represented by L onto the stack. Returns 1 if 
 * this thread is the main thread of its state.
 */
/**
 * 将 L 压入栈中, 若主线程为 L 返回 true，否则返回 0
 */
LUA_API int lua_pushthread (lua_State *L) {
  lua_lock(L);
  setthvalue(L, L->top, L);
  api_incr_top(L);
  lua_unlock(L);
  return (G(L)->mainthread == L);
}



/*
** get functions (Lua -> stack)
*/


/*
 * Pushes onto the stack the value of the global name. Returns the type
 * of that value.
 */
/**
 * 获取 gloable_state 中  l_registry 表中对应变量的值, 保存在栈顶，返回变量类型
 * name: 变量名
 * 
 * see http://www.lua.org/manual/5.3/manual.html#4.5 
 */
LUA_API int lua_getglobal (lua_State *L, const char *name) {
  Table *reg = hvalue(&G(L)->l_registry);
  const TValue *gt;  /* global table */
  lua_lock(L);
  gt = luaH_getint(reg, LUA_RIDX_GLOBALS);
  setsvalue2s(L, L->top++, luaS_new(L, name));
  luaV_gettable(L, gt, L->top - 1, L->top - 1);
  lua_unlock(L);
  return ttnov(L->top - 1);
}


/*
 * Pushes onto the stack the value t[k], where t is the value at the
 * given index and k is the value at the top of the stack.

 * This function pops the key from the stack, pushing the resulting
 * value in its place. As in Lua, this function may trigger a 
 * metamethod for the "index" event (see §2.4).

 * Returns the type of the pushed value.
 */
/**
 * 获取在位置 idx 的表中以栈顶元素为 key 的值，结果保存在栈顶
 * 
 * 返回结果类型
 */
LUA_API int lua_gettable (lua_State *L, int idx) {
  StkId t;
  lua_lock(L);
  t = index2addr(L, idx);
  luaV_gettable(L, t, L->top - 1, L->top - 1);
  lua_unlock(L);
  return ttnov(L->top - 1);
}

/*
 * Pushes onto the stack the value t[k], where t is the value at the
 * given index. As in Lua, this function may trigger a metamethod for
 * the "index" event (see §2.4).

 * Returns the type of the pushed value.
 */
LUA_API int lua_getfield (lua_State *L, int idx, const char *k) {
  StkId t;
  lua_lock(L);
  t = index2addr(L, idx);
  setsvalue2s(L, L->top, luaS_new(L, k));
  api_incr_top(L);
  luaV_gettable(L, t, L->top - 1, L->top - 1);
  lua_unlock(L);
  return ttnov(L->top - 1);
}


/*
 * Pushes onto the stack the value t[i], where t is the value at the
 * given index. As in Lua, this function may trigger a metamethod for
 * the "index" event (see §2.4).

 * Returns the type of the pushed value.
 */
LUA_API int lua_geti (lua_State *L, int idx, lua_Integer n) {
  StkId t;
  lua_lock(L);
  t = index2addr(L, idx);
  setivalue(L->top, n);
  api_incr_top(L);
  luaV_gettable(L, t, L->top - 1, L->top - 1);
  lua_unlock(L);
  return ttnov(L->top - 1);
}


/*
 * Similar to lua_gettable, but does a raw access (i.e., without 
 * metamethods).
 */
LUA_API int lua_rawget (lua_State *L, int idx) {
  StkId t;
  lua_lock(L);
  t = index2addr(L, idx);
  api_check(ttistable(t), "table expected");
  setobj2s(L, L->top - 1, luaH_get(hvalue(t), L->top - 1));
  lua_unlock(L);
  return ttnov(L->top - 1);
}


/*
 * Pushes onto the stack the value t[n], where t is the table at the
 * given index. The access is raw; that is, it does not invoke 
 * metamethods.
 * Returns the type of the pushed value.
 */
LUA_API int lua_rawgeti (lua_State *L, int idx, lua_Integer n) {
  StkId t;
  lua_lock(L);
  t = index2addr(L, idx);
  api_check(ttistable(t), "table expected");
  setobj2s(L, L->top, luaH_getint(hvalue(t), n));
  api_incr_top(L);
  lua_unlock(L);
  return ttnov(L->top - 1);
}


/*
 * Pushes onto the stack the value t[k], where t is the table at the
 * given index and k is the pointer p represented as a light userdata.
 * The access is raw; that is, it does not invoke metamethods.
 *
 * Returns the type of the pushed value.
 */
LUA_API int lua_rawgetp (lua_State *L, int idx, const void *p) {
  StkId t;
  TValue k;
  lua_lock(L);
  t = index2addr(L, idx);
  api_check(ttistable(t), "table expected");
  setpvalue(&k, cast(void *, p));
  setobj2s(L, L->top, luaH_get(hvalue(t), &k));
  api_incr_top(L);
  lua_unlock(L);
  return ttnov(L->top - 1);
}

/*
 * Creates a new empty table and pushes it onto the stack. Parameter 
 * narr is a hint for how many elements the table will have as a 
 * sequence; parameter nrec is a hint for how many other elements the
 * table will have. Lua may use these hints to preallocate memory for
 * the new table. This pre-allocation is useful for performance when
 * you know in advance how many elements the table will have. Otherwise
 * you can use the function lua_newtable.
 */
/**
 * 在栈顶建一个空的 Table
 */
LUA_API void lua_createtable (lua_State *L, int narray, int nrec) {
  Table *t;
  lua_lock(L);
  luaC_checkGC(L);
  t = luaH_new(L);
  sethvalue(L, L->top, t);
  api_incr_top(L);
  if (narray > 0 || nrec > 0)
    luaH_resize(L, t, narray, nrec);
  lua_unlock(L);
}


/*
 * If the value at the given index has a metatable, the function pushes
 * that metatable onto the stack and returns 1. Otherwise, the function
 * returns 0 and pushes nothing on the stack.
 */
/**
 * 获取对象元表, 放在栈顶, 成功返回1，否则0
 */
LUA_API int lua_getmetatable (lua_State *L, int objindex) {
  const TValue *obj;
  Table *mt;
  int res = 0;
  lua_lock(L);
  obj = index2addr(L, objindex);
  switch (ttnov(obj)) {
    case LUA_TTABLE:
      mt = hvalue(obj)->metatable;
      break;
    case LUA_TUSERDATA:
      mt = uvalue(obj)->metatable;
      break;
    default:
      mt = G(L)->mt[ttnov(obj)];
      break;
  }
  if (mt != NULL) {
    sethvalue(L, L->top, mt);
    api_incr_top(L);
    res = 1;
  }
  lua_unlock(L);
  return res;
}

/*
 * Pushes onto the stack the Lua value associated with the userdata 
 * at the given index.

 * Returns the type of the pushed value.
 */
/* 将 idx 位置的 Udata 元素放在栈顶 */
LUA_API int lua_getuservalue (lua_State *L, int idx) {
  StkId o;
  lua_lock(L);
  o = index2addr(L, idx);
  api_check(ttisfulluserdata(o), "full userdata expected");
  getuservalue(L, uvalue(o), L->top);
  api_incr_top(L);
  lua_unlock(L);
  return ttnov(L->top - 1);
}


/*
** set functions (stack -> Lua)
*/

/*
 * Pops a value from the stack and sets it as the new value of global
 * name.
 */
/* see http://www.lua.org/manual/5.3/manual.html#4.5 */
LUA_API void lua_setglobal (lua_State *L, const char *name) {
  Table *reg = hvalue(&G(L)->l_registry);
  const TValue *gt;  /* global table */
  lua_lock(L);
  api_checknelems(L, 1);
  gt = luaH_getint(reg, LUA_RIDX_GLOBALS);
  setsvalue2s(L, L->top++, luaS_new(L, name));
  luaV_settable(L, gt, L->top - 1, L->top - 2);
  L->top -= 2;  /* pop value and key */
  lua_unlock(L);
}


/*
 * Does the equivalent to t[k] = v, where t is the value at the given
 * index, v is the value at the top of the stack, and k is the value
 * just below the top.

 * This function pops both the key and the value from the stack. As 
 * in Lua, this function may trigger a metamethod for the "newindex"
 * event (see §2.4).
 */
LUA_API void lua_settable (lua_State *L, int idx) {
  StkId t;
  lua_lock(L);
  api_checknelems(L, 2);
  t = index2addr(L, idx);
  luaV_settable(L, t, L->top - 2, L->top - 1);
  L->top -= 2;  /* pop index and value */
  lua_unlock(L);
}

/*
 * Does the equivalent to t[k] = v, where t is the value at the given
 * index and v is the value at the top of the stack.

 * This function pops the value from the stack. As in Lua, this 
 * function may trigger a metamethod for the "newindex" event (see §2.4).
 */
/**
 * t[k] = v, t 为 stack[idx], v 为栈顶元素
 * 最后弹出 v, 可能使用元方法
 */
LUA_API void lua_setfield (lua_State *L, int idx, const char *k) {
  StkId t;
  lua_lock(L);
  api_checknelems(L, 1);
  t = index2addr(L, idx);
  setsvalue2s(L, L->top++, luaS_new(L, k));
  luaV_settable(L, t, L->top - 1, L->top - 2);
  L->top -= 2;  /* pop value and key */
  lua_unlock(L);
}

/*
 * Does the equivalent to t[n] = v, where t is the value at the given
 * index and v is the value at the top of the stack.

 * This function pops the value from the stack. As in Lua, this 
 * function may trigger a metamethod for the "newindex" event (see §2.4).
 */
LUA_API void lua_seti (lua_State *L, int idx, lua_Integer n) {
  StkId t;
  lua_lock(L);
  api_checknelems(L, 1);
  t = index2addr(L, idx);
  setivalue(L->top++, n);
  luaV_settable(L, t, L->top - 1, L->top - 2);
  L->top -= 2;  /* pop value and key */
  lua_unlock(L);
}

/*
 * Does the equivalent to t[n] = v, where t is the value at the given
 * index and v is the value at the top of the stack.

 * This function pops the value from the stack. As in Lua, this 
 * function may trigger a metamethod for the "newindex" event (see §2.4).
 */
LUA_API void lua_rawset (lua_State *L, int idx) {
  StkId o;
  Table *t;
  lua_lock(L);
  api_checknelems(L, 2);
  o = index2addr(L, idx);
  api_check(ttistable(o), "table expected");
  t = hvalue(o);
  setobj2t(L, luaH_set(L, t, L->top-2), L->top-1);
  /* TODO */
  invalidateTMcache(t);
  luaC_barrierback(L, t, L->top-1);
  L->top -= 2;
  lua_unlock(L);
}

/*
 * Does the equivalent of t[i] = v, where t is the table at the given
 * index and v is the value at the top of the stack.
 *
 * This function pops the value from the stack. The assignment is raw;
 * that is, it does not invoke metamethods.
 */
LUA_API void lua_rawseti (lua_State *L, int idx, lua_Integer n) {
  StkId o;
  Table *t;
  lua_lock(L);
  api_checknelems(L, 1);
  o = index2addr(L, idx);
  api_check(ttistable(o), "table expected");
  t = hvalue(o);
  luaH_setint(L, t, n, L->top - 1);
  /* TODO */
  luaC_barrierback(L, t, L->top-1);
  L->top--;
  lua_unlock(L);
}


LUA_API void lua_rawsetp (lua_State *L, int idx, const void *p) {
  StkId o;
  Table *t;
  TValue k;
  lua_lock(L);
  api_checknelems(L, 1);
  o = index2addr(L, idx);
  api_check(ttistable(o), "table expected");
  t = hvalue(o);
  setpvalue(&k, cast(void *, p));
  setobj2t(L, luaH_set(L, t, &k), L->top - 1);
  /* TODO */
  luaC_barrierback(L, t, L->top - 1);
  L->top--;
  lua_unlock(L);
}

/*
 * Pops a table from the stack and sets it as the new metatable for 
 * the value at the given index.
 */
/**
 * 若 objindex 元素不是表，则会设置该元素类型的全局默认元表
 */
LUA_API int lua_setmetatable (lua_State *L, int objindex) {
  TValue *obj;
  Table *mt;
  lua_lock(L);
  api_checknelems(L, 1);
  obj = index2addr(L, objindex);
  if (ttisnil(L->top - 1))
    mt = NULL;
  else {
    api_check(ttistable(L->top - 1), "table expected");
    mt = hvalue(L->top - 1);
  }
  switch (ttnov(obj)) {
    case LUA_TTABLE: {
      hvalue(obj)->metatable = mt;
	  /* TODO */
      if (mt) {
        luaC_objbarrier(L, gcvalue(obj), mt);
        luaC_checkfinalizer(L, gcvalue(obj), mt);
      }
      break;
    }
    case LUA_TUSERDATA: {
      uvalue(obj)->metatable = mt;
      if (mt) {
        luaC_objbarrier(L, uvalue(obj), mt);
        luaC_checkfinalizer(L, gcvalue(obj), mt);
      }
      break;
    }
    default: {
      /* 设置基本类型元表 */
      G(L)->mt[ttnov(obj)] = mt;
      break;
    }
  }
  L->top--;
  lua_unlock(L);
  return 1;
}

/*
 * Pops a value from the stack and sets it as the new value associated
 * to the userdata at the given index
 */
LUA_API void lua_setuservalue (lua_State *L, int idx) {
  StkId o;
  lua_lock(L);
  api_checknelems(L, 1);
  o = index2addr(L, idx);
  api_check(ttisfulluserdata(o), "full userdata expected");
  setuservalue(L, uvalue(o), L->top - 1);
  luaC_barrier(L, gcvalue(o), L->top - 1);
  L->top--;
  lua_unlock(L);
}


/*
** 'load' and 'call' functions (run Lua code)
*/


#define checkresults(L,na,nr) \
     api_check((nr) == LUA_MULTRET || (L->ci->top - L->top >= (nr) - (na)), \
	"results from function overflow current stack size")


/*
 * This function behaves exactly like lua_call, but allows the called
 * function to yield (see §4.7 http://www.lua.org/manual/5.3/manual.html#4.7).
 */
LUA_API void lua_callk (lua_State *L, int nargs, int nresults,
                        lua_KContext ctx, lua_KFunction k) {
  StkId func;
  lua_lock(L);
  api_check(k == NULL || !isLua(L->ci),
    "cannot use continuations inside hooks");
  api_checknelems(L, nargs+1);
  api_check(L->status == LUA_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  func = L->top - (nargs+1);
  if (k != NULL && L->nny == 0) {  /* need to prepare continuation? */
    L->ci->u.c.k = k;  /* save continuation */
    L->ci->u.c.ctx = ctx;  /* save context */
    luaD_call(L, func, nresults, 1);  /* do the call */
  }
  else  /* no continuation or no yieldable */
    luaD_call(L, func, nresults, 0);  /* just do the call */
  adjustresults(L, nresults);
  lua_unlock(L);
}



/*
** Execute a protected call.
*/
struct CallS {  /* data to 'f_call' */
  StkId func;
  int nresults;
};


static void f_call (lua_State *L, void *ud) {
  struct CallS *c = cast(struct CallS *, ud);
  luaD_call(L, c->func, c->nresults, 0);
}



/*
 * This function behaves exactly like lua_pcall, but allows the called 
 * function to yield (see §4.7 http://www.lua.org/manual/5.3/manual.html#4.7).
 */
LUA_API int lua_pcallk (lua_State *L, int nargs, int nresults, int errfunc,
                        lua_KContext ctx, lua_KFunction k) {
  struct CallS c;
  int status;
  ptrdiff_t func;
  lua_lock(L);
  api_check(k == NULL || !isLua(L->ci),
    "cannot use continuations inside hooks");
  api_checknelems(L, nargs+1);
  api_check(L->status == LUA_OK, "cannot do calls on non-normal thread");
  checkresults(L, nargs, nresults);
  if (errfunc == 0)
    func = 0;
  else {
    StkId o = index2addr(L, errfunc);
    api_checkstackindex(errfunc, o);
    func = savestack(L, o);
  }
  c.func = L->top - (nargs+1);  /* function to be called */
  if (k == NULL || L->nny > 0) {  /* no continuation or no yieldable? */
    c.nresults = nresults;  /* do a 'conventional' protected call */
    status = luaD_pcall(L, f_call, &c, savestack(L, c.func), func);
  }
  else {  /* prepare continuation (call is already protected by 'resume') */
    CallInfo *ci = L->ci;
    ci->u.c.k = k;  /* save continuation */
    ci->u.c.ctx = ctx;  /* save context */
    /* save information for error recovery */
    ci->extra = savestack(L, c.func);
    ci->u.c.old_errfunc = L->errfunc;
    L->errfunc = func;
    setoah(ci->callstatus, L->allowhook);  /* save value of 'allowhook' */
    ci->callstatus |= CIST_YPCALL;  /* function can do error recovery */
    luaD_call(L, c.func, nresults, 1);  /* do the call */
    ci->callstatus &= ~CIST_YPCALL;
    L->errfunc = ci->u.c.old_errfunc;
    status = LUA_OK;  /* if it is here, there were no errors */
  }
  adjustresults(L, nresults);
  lua_unlock(L);
  return status;
}


/*
 * Loads a Lua chunk without running it. If there are no errors, lua_load
 * pushes the compiled chunk as a Lua function on top of the stack.
 * Otherwise, it pushes an error message.
 */
LUA_API int lua_load (lua_State *L, lua_Reader reader, void *data,
                      const char *chunkname, const char *mode) {
  ZIO z;
  int status;
  lua_lock(L);
  if (!chunkname) chunkname = "?";
  luaZ_init(L, &z, reader, data);
  status = luaD_protectedparser(L, &z, chunkname, mode);
  if (status == LUA_OK) {  /* no errors? */
    LClosure *f = clLvalue(L->top - 1);  /* get newly created function */
    if (f->nupvalues >= 1) {  /* does it have an upvalue? */
      /* get global table from registry */
      Table *reg = hvalue(&G(L)->l_registry);
      const TValue *gt = luaH_getint(reg, LUA_RIDX_GLOBALS);
      /* set global table as 1st upvalue of 'f' (may be LUA_ENV) */
      setobj(L, f->upvals[0]->v, gt);
      luaC_upvalbarrier(L, f->upvals[0]);
    }
  }
  lua_unlock(L);
  return status;
}

/*
 * Dumps a function as a binary chunk. Receives a Lua function on 
 * the top of the stack and produces a binary chunk that, if loaded
 * again, results in a function equivalent to the one dumped. As 
 * it produces parts of the chunk, lua_dump calls function writer
 * (see lua_Writer) with the given data to write them.

 * If strip is true, the binary representation is created without
 * debug information about the function.

 * The value returned is the error code returned by the last call 
 * to the writer; 0 means no errors.

 * This function does not pop the Lua function from the stack.
 */
/**
 * dump lua 函数（闭包）,若是其他类型直接返回 1
 */
LUA_API int lua_dump (lua_State *L, lua_Writer writer, void *data, int strip) {
  int status;
  TValue *o;
  lua_lock(L);
  api_checknelems(L, 1);
  o = L->top - 1;
  if (isLfunction(o))
    status = luaU_dump(L, getproto(o), writer, data, strip);
  else
    status = 1;
  lua_unlock(L);
  return status;
}


/*
 * Returns the status of the thread L.

 * The status can be 0 (LUA_OK) for a normal thread, an error code
 * if the thread finished the execution of a lua_resume with an 
 * error, or LUA_YIELD if the thread is suspended.
 *
 * You can only call functions in threads with status LUA_OK. You can
 * resume threads with status LUA_OK (to start a new coroutine) or 
 * LUA_YIELD (to resume a coroutine).
 */
LUA_API int lua_status (lua_State *L) {
  return L->status;
}


/*
** Garbage-collection function
*/

/*
 * This function performs several tasks, according to the value of 
 * the parameter what:
 *
 *   LUA_GCSTOP: stops the garbage collector.
 *   LUA_GCRESTART: restarts the garbage collector.
 *   LUA_GCCOLLECT: performs a full garbage-collection cycle.
 *   LUA_GCCOUNT: returns the current amount of memory (in Kbytes)
 *     in use by Lua.
 *   LUA_GCCOUNTB: returns the remainder of dividing the current amount
 *     of bytes of memory in use by Lua by 1024.
 *   LUA_GCSTEP: performs an incremental step of garbage collection.
 *   LUA_GCSETPAUSE: sets data as the new value for the pause of the
 *     collector (see §2.5) and returns the previous value of the pause.
 *   LUA_GCSETSTEPMUL: sets data as the new value for the step 
 *     multiplier of the collector (see §2.5) and returns the previous
 *     value of the step multiplier.
 *   LUA_GCISRUNNING: returns a boolean that tells whether the 
 *     collector is running (i.e., not stopped).
 */
LUA_API int lua_gc (lua_State *L, int what, int data) {
  int res = 0;
  global_State *g;
  lua_lock(L);
  g = G(L);
  switch (what) {
    case LUA_GCSTOP: {
      g->gcrunning = 0;
      break;
    }
    case LUA_GCRESTART: {
      luaE_setdebt(g, 0);
      g->gcrunning = 1;
      break;
    }
    case LUA_GCCOLLECT: {
      luaC_fullgc(L, 0);
      break;
    }
    case LUA_GCCOUNT: {
      /* GC values are expressed in Kbytes: #bytes/2^10 */
      res = cast_int(gettotalbytes(g) >> 10);
      break;
    }
    case LUA_GCCOUNTB: {
      res = cast_int(gettotalbytes(g) & 0x3ff);
      break;
    }
    case LUA_GCSTEP: {
      l_mem debt = 1;  /* =1 to signal that it did an actual step */
      int oldrunning = g->gcrunning;
      g->gcrunning = 1;  /* allow GC to run */
      if (data == 0) {
        luaE_setdebt(g, -GCSTEPSIZE);  /* to do a "small" step */
        luaC_step(L);
      }
      else {  /* add 'data' to total debt */
        debt = cast(l_mem, data) * 1024 + g->GCdebt;
        luaE_setdebt(g, debt);
        luaC_checkGC(L);
      }
      g->gcrunning = oldrunning;  /* restore previous state */
      if (debt > 0 && g->gcstate == GCSpause)  /* end of cycle? */
        res = 1;  /* signal it */
      break;
    }
    case LUA_GCSETPAUSE: {
      res = g->gcpause;
      g->gcpause = data;
      break;
    }
    case LUA_GCSETSTEPMUL: {
      res = g->gcstepmul;
      if (data < 40) data = 40;  /* avoid ridiculous low values (and 0) */
      g->gcstepmul = data;
      break;
    }
    case LUA_GCISRUNNING: {
      res = g->gcrunning;
      break;
    }
    default: res = -1;  /* invalid option */
  }
  lua_unlock(L);
  return res;
}



/*
** miscellaneous functions
*/


/*
 * Generates a Lua error, using the value at the top of the stack
 * as the error object. This function does a long jump, and therefore
 * never returns (see luaL_error).
 */
LUA_API int lua_error (lua_State *L) {
  lua_lock(L);
  api_checknelems(L, 1);
  luaG_errormsg(L);
  /* code unreachable; will unlock when control actually leaves the kernel */
  return 0;  /* to avoid warnings */
}


/**
 * 
 * 首先查找 key 在 table 中的索引顺序位置，查找顺序是先array，再 
 * node hash table, 然后将这个位置后的第一个非 nil 元素的 key, value 
 * 分别压入栈中， 对于 array 来说，key 就是数组下标加 1.
 * 
 * key 就是栈顶元素
 * 
 * 如果找到非 nil 元素，返回 1, 否则返回 0.
 */
LUA_API int lua_next (lua_State *L, int idx) {
  StkId t;
  int more;
  lua_lock(L);
  t = index2addr(L, idx);
  api_check(ttistable(t), "table expected");
  more = luaH_next(L, hvalue(t), L->top - 1);
  if (more) {
    api_incr_top(L);
  }
  else  /* no more elements */
    L->top -= 1;  /* remove key */
  lua_unlock(L);
  return more;
}

/*
 * Concatenates the n values at the top of the stack, pops them, 
 * and leaves the result at the top. If n is 1, the result is the
 * single value on the stack (that is, the function does nothing);
 * if n is 0, the result is the empty string. Concatenation is 
 * performed following the usual semantics of Lua (see §3.4.6).
 *
 * The string concatenation operator in Lua is denoted by two 
 * dots ('..'). If both operands are strings or numbers, then 
 * they are converted to strings according to the rules described 
 * in §3.4.3. Otherwise, the __concat metamethod is called (see §2.4).
 */
LUA_API void lua_concat (lua_State *L, int n) {
  lua_lock(L);
  api_checknelems(L, n);
  if (n >= 2) {
    luaC_checkGC(L);
    luaV_concat(L, n);
  }
  else if (n == 0) {  /* push empty string */
    setsvalue2s(L, L->top, luaS_newlstr(L, "", 0));
    api_incr_top(L);
  }
  /* else n == 1; nothing to do */
  lua_unlock(L);
}


/*
 * Returns the length of the value at the given index. It is equivalent
 * to the '#' operator in Lua (see §3.4.7) and may trigger a metamethod
 * for the "length" event (see §2.4). The result is pushed on the stack.
 */
LUA_API void lua_len (lua_State *L, int idx) {
  StkId t;
  lua_lock(L);
  t = index2addr(L, idx);
  luaV_objlen(L, L->top, t);
  api_incr_top(L);
  lua_unlock(L);
}

/*
 * Returns the memory-allocation function of a given state. If 
 * ud is not NULL, Lua stores in *ud the opaque pointer given 
 * when the memory-allocator function was set.
 */
LUA_API lua_Alloc lua_getallocf (lua_State *L, void **ud) {
  lua_Alloc f;
  lua_lock(L);
  if (ud) *ud = G(L)->ud;
  f = G(L)->frealloc;
  lua_unlock(L);
  return f;
}

/*
 * Changes the allocator function of a given state to f with user 
 * data ud.
 */
LUA_API void lua_setallocf (lua_State *L, lua_Alloc f, void *ud) {
  lua_lock(L);
  G(L)->ud = ud;
  G(L)->frealloc = f;
  lua_unlock(L);
}


/*
 * This function allocates a new block of memory with the given size,
 * pushes onto the stack a new full userdata with the block address, 
 * and returns this address. The host program can freely use this memory.
 */
/**
 * 分配一个大小为 size 的Udata型数据在栈顶，并返回Udata数据区域的指针
 */
LUA_API void *lua_newuserdata (lua_State *L, size_t size) {
  Udata *u;
  lua_lock(L);
  luaC_checkGC(L);
  u = luaS_newudata(L, size);

  /* 将Udata u 压入栈中 */
  setuvalue(L, L->top, u);
  /* 栈顶指针加1 */
  api_incr_top(L);
  lua_unlock(L);
  return getudatamem(u);
}



/**
 * fi: 函数位置
 * val: 第 n 个 upvalue 指针的地址
 * owner: C closure 才有用, *owner = f
 * uv: 第 n 个 upvalue 的值
 */
static const char *aux_upvalue (StkId fi, int n, TValue **val,
                                CClosure **owner, UpVal **uv) {
  switch (ttype(fi)) {
    case LUA_TCCL: {  /* C closure */
      CClosure *f = clCvalue(fi);
      if (!(1 <= n && n <= f->nupvalues)) return NULL;
      *val = &f->upvalue[n-1];
      if (owner) *owner = f;
      return "";
    }
    case LUA_TLCL: {  /* Lua closure */
      LClosure *f = clLvalue(fi);
      TString *name;
      Proto *p = f->p;
      if (!(1 <= n && n <= p->sizeupvalues)) return NULL;
      *val = f->upvals[n-1]->v;
      if (uv) *uv = f->upvals[n - 1];
      name = p->upvalues[n-1].name;
      return (name == NULL) ? "(*no name)" : getstr(name);
    }
    default: return NULL;  /* not a closure */
  }
}


/*
 * Gets information about a closure's upvalue. (For Lua functions,
 * upvalues are the external local variables that the function 
 * uses, and that are consequently included in its closure.) 
 * lua_getupvalue gets the index n of an upvalue, pushes the upvalue's 
 * value onto the stack, and returns its name. funcindex points to the
 * closure in the stack. (Upvalues have no particular order, as they 
 * are active through the whole function. So, they are numbered in an
 * arbitrary order.)

 * Returns NULL (and pushes nothing) when the index is greater than the
 * number of upvalues. For C functions, this function uses the empty 
 * string "" as a name for all upvalues.
*/
LUA_API const char *lua_getupvalue (lua_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = NULL;  /* to avoid warnings */
  lua_lock(L);
  name = aux_upvalue(index2addr(L, funcindex), n, &val, NULL, NULL);
  if (name) {
    setobj2s(L, L->top, val);
    api_incr_top(L);
  }
  lua_unlock(L);
  return name;
}

/*
 * Sets the value of a closure's upvalue. It assigns the value at
 * the top of the stack to the upvalue and returns its name. It also
 * pops the value from the stack. Parameters funcindex and n are as 
 * in the lua_getupvalue (see lua_getupvalue).
 *
 * Returns NULL (and pops nothing) when the index is greater than the
 * number of upvalues.
*/
LUA_API const char *lua_setupvalue (lua_State *L, int funcindex, int n) {
  const char *name;
  TValue *val = NULL;  /* to avoid warnings */
  CClosure *owner = NULL;
  UpVal *uv = NULL;
  StkId fi;
  lua_lock(L);
  fi = index2addr(L, funcindex);
  api_checknelems(L, 1);
  name = aux_upvalue(fi, n, &val, &owner, &uv);
  if (name) {
    L->top--;
    setobj(L, val, L->top);
    if (owner) { luaC_barrier(L, owner, L->top); }
    else if (uv) { luaC_upvalbarrier(L, uv); }
  }
  lua_unlock(L);
  return name;
}


/**
 * fidx: 闭包位置
 * n: 第 n 个upvalue
 * pf: 不为NULL时返回闭包地址
 * 返回 upvalue 的地址
 */
static UpVal **getupvalref (lua_State *L, int fidx, int n, LClosure **pf) {
  LClosure *f;
  StkId fi = index2addr(L, fidx);
  api_check(ttisLclosure(fi), "Lua function expected");
  f = clLvalue(fi);
  api_check((1 <= n && n <= f->p->sizeupvalues), "invalid upvalue index");
  if (pf) *pf = f;
  return &f->upvals[n - 1];  /* get its upvalue pointer */
}



/*
 * Returns a unique identifier for the upvalue numbered n from the 
 * closure at index funcindex. Parameters funcindex and n are as in 
 * the lua_getupvalue (see lua_getupvalue) (but n cannot be greater
 * than the number of upvalues).
 *
 * These unique identifiers allow a program to check whether different
 * closures share upvalues. Lua closures that share an upvalue (that is, 
 * that access a same external local variable) will return identical ids
 * for those upvalue indices.
*/
/**
 * 实际上就是返回 upvalue 的地址, &f->upvalue[n - 1]
 */
LUA_API void *lua_upvalueid (lua_State *L, int fidx, int n) {
  StkId fi = index2addr(L, fidx);
  switch (ttype(fi)) {
    case LUA_TLCL: {  /* lua closure */
      return *getupvalref(L, fidx, n, NULL);
    }
    case LUA_TCCL: {  /* C closure */
      CClosure *f = clCvalue(fi);
      api_check(1 <= n && n <= f->nupvalues, "invalid upvalue index");
      return &f->upvalue[n - 1];
    }
    default: {
      api_check(0, "closure expected");
      return NULL;
    }
  }
}


/*
 * Make the n1-th upvalue of the Lua closure at index funcindex1 refer
 * to the n2-th upvalue of the Lua closure at index funcindex2.
 */
/* 并更改相关引用次数 */
LUA_API void lua_upvaluejoin (lua_State *L, int fidx1, int n1,
                                            int fidx2, int n2) {
  LClosure *f1;
  UpVal **up1 = getupvalref(L, fidx1, n1, &f1);
  UpVal **up2 = getupvalref(L, fidx2, n2, NULL);
  luaC_upvdeccount(L, *up1);
  *up1 = *up2;
  (*up1)->refcount++;
  if (upisopen(*up1)) (*up1)->u.open.touched = 1;
  luaC_upvalbarrier(L, *up1);
}


