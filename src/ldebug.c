/*
** $Id: ldebug.c,v 2.110 2015/01/02 12:52:22 roberto Exp $
** Debug Interface
** See Copyright Notice in lua.h
*/

#define ldebug_c
#define LUA_CORE

#include "lprefix.h"


#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include "lua.h"

#include "lapi.h"
#include "lcode.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"



#define noLuaClosure(f)		((f) == NULL || (f)->c.tt == LUA_TCCL)


static const char *getfuncname (lua_State *L, CallInfo *ci, const char **name);


/**
 * 当前指令 pc 相对位置偏移
 */
static int currentpc (CallInfo *ci) {
  lua_assert(isLua(ci));
  return pcRel(ci->u.l.savedpc, ci_func(ci)->p);
}


/**
 * 获取当前指令对应的源码行号, 若没有行号信息则返回 -1
 */
static int currentline (CallInfo *ci) {
  return getfuncline(ci_func(ci)->p, currentpc(ci));
}


/*
** this function can be called asynchronous (e.g. during a signal)
*/
/**
 * Sets the given function as a hook. The string mask and the number 
 * count describe when the hook will be called. The string mask may 
 * have any combination of the following characters, with the given
 * meaning:
 *
 * 'c': the hook is called every time Lua calls a function;
 * 'r': the hook is called every time Lua returns from a function;
 * 'l': the hook is called every time Lua enters a new line of code.
 * 
 * Moreover, with a count different from zero, the hook is called also
 * after every count instructions.
 *
 * When called without arguments, debug.sethook turns off the hook.
 *
 * When the hook is called, its first parameter is a string describing
 * the event that has triggered its call: "call" (or "tail call"), 
 * "return", "line", and "count". For line events, the hook also gets the
 * new line number as its second parameter. Inside a hook, you can call 
 * getinfo with level 2 to get more information about the running function
 * (level 0 is the getinfo function, and level 1 is the hook function).
 */
LUA_API void lua_sethook (lua_State *L, lua_Hook func, int mask, int count) {
  if (func == NULL || mask == 0) {  /* turn off hooks? */
    mask = 0;
    func = NULL;
  }
  if (isLua(L->ci))
    L->oldpc = L->ci->u.l.savedpc;
  L->hook = func;
  L->basehookcount = count;
  resethookcount(L);
  L->hookmask = cast_byte(mask);
}


LUA_API lua_Hook lua_gethook (lua_State *L) {
  return L->hook;
}


LUA_API int lua_gethookmask (lua_State *L) {
  return L->hookmask;
}


LUA_API int lua_gethookcount (lua_State *L) {
  return L->basehookcount;
}

/**
 * Gets information about the interpreter runtime stack.
 *
 * This function fills parts of a lua_Debug structure with an 
 * identification of the activation record of the function executing
 * at a given level. Level 0 is the current running function, whereas
 * level n+1 is the function that has called level n (except for tail 
 * calls, which do not count on the stack). When there are no errors, 
 * lua_getstack returns 1; when called with a level greater than the 
 * stack depth, it returns 0.
 */
LUA_API int lua_getstack (lua_State *L, int level, lua_Debug *ar) {
  int status;
  CallInfo *ci;
  if (level < 0) return 0;  /* invalid (negative) level */
  lua_lock(L);
  for (ci = L->ci; level > 0 && ci != &L->base_ci; ci = ci->previous)
    level--;
  if (level == 0 && ci != &L->base_ci) {  /* level found? */
    status = 1;
    ar->i_ci = ci;
  }
  else status = 0;  /* no such level */
  lua_unlock(L);
  return status;
}


/**
 * 获取 upvalue 名字, 默认为 '?'
 */
static const char *upvalname (Proto *p, int uv) {
  TString *s = check_exp(uv < p->sizeupvalues, p->upvalues[uv].name);
  if (s == NULL) return "?";
  else return getstr(s);
}


/**
 * stack[pos] 返回函数第 n 个变长参数;
 * 存在返回 "*(vararg)", 否则返回 NULL
 */
static const char *findvararg (CallInfo *ci, int n, StkId *pos) {
  int nparams = clLvalue(ci->func)->p->numparams;
  if (n >= ci->u.l.base - ci->func - nparams)
    return NULL;  /* no such vararg */
  else {
    *pos = ci->func + nparams + n;
    return "(*vararg)";  /* generic name for any vararg */
  }
}


/**
 * 返回调用 ci 执行到当前位置时的第 n 个局部变量的名字, pos 返回
 * 变量位置; 变量不存在返回 NULL
 *
 * n 为负数表示获取 第 -n 个 变长参数名, 此时 pos 不返回变量位置
 */
static const char *findlocal (lua_State *L, CallInfo *ci, int n,
                              StkId *pos) {
  const char *name = NULL;
  StkId base;
  if (isLua(ci)) {
    if (n < 0)  /* access to vararg values? */
      return findvararg(ci, -n, pos);
    else {
      base = ci->u.l.base;
      name = luaF_getlocalname(ci_func(ci)->p, n, currentpc(ci));
    }
  }
  else
    base = ci->func + 1;
  if (name == NULL) {  /* no 'standard' name? */
    StkId limit = (ci == L->ci) ? L->top : ci->next->func;
    if (limit - base >= n && n > 0)  /* is 'n' inside 'ci' stack? */
      name = "(*temporary)";  /* generic name for any valid slot */
    else
      return NULL;  /* no name */
  }
  *pos = base + (n - 1);
  return name;
}


/**
 * Gets information about a local variable of a given activation record 
 * or a given function.
 * 
 * In the first case, the parameter ar must be a valid activation
 * record that was filled by a previous call to lua_getstack or given 
 * as argument to a hook (see lua_Hook). The index n selects which local 
 * variable to inspect; see debug.getlocal for details about variable 
 * indices and names.
 *
 * lua_getlocal pushes the variable's value onto the stack and returns its 
 * name.
 *
 * In the second case, ar must be NULL and the function to be inspected must 
 * be at the top of the stack. In this case, only parameters of Lua functions 
 * are visible (as there is no information about what variables are active) 
 * and no values are pushed onto the stack.
 *
 * Returns NULL (and pushes nothing) when the index is greater than the 
 * number of active local variables.
 */
LUA_API const char *lua_getlocal (lua_State *L, const lua_Debug *ar, int n) {
  const char *name;
  lua_lock(L);
  if (ar == NULL) {  /* information about non-active function? */
    if (!isLfunction(L->top - 1))  /* not a Lua function? */
      name = NULL;
    else  /* consider live variables at function start (parameters) */
      name = luaF_getlocalname(clLvalue(L->top - 1)->p, n, 0);
  }
  else {  /* active function; get information through 'ar' */
    StkId pos = 0;  /* to avoid warnings */
    name = findlocal(L, ar->i_ci, n, &pos);
    if (name) {
      setobj2s(L, L->top, pos);
      api_incr_top(L);
    }
  }
  lua_unlock(L);
  return name;
}


/**
 * Sets the value of a local variable of a given activation record. 
 * Parameters ar and n are as in lua_getlocal (see lua_getlocal). 
 * lua_setlocal assigns the value at the top of the stack to the
 * variable and returns its name. It also pops the value from the stack.
 *
 * Returns NULL (and pops nothing) when the index is greater than the 
 * number of active local variables.
 */
/**
 * 修改值在栈顶
 */
LUA_API const char *lua_setlocal (lua_State *L, const lua_Debug *ar, int n) {
  StkId pos = 0;  /* to avoid warnings */
  const char *name = findlocal(L, ar->i_ci, n, &pos);
  lua_lock(L);
  if (name) {
    setobjs2s(L, pos, L->top - 1);
    L->top--;  /* pop value */
  }
  lua_unlock(L);
  return name;
}


/**
 * 往 ar 中填充函数相关信息
 */
static void funcinfo (lua_Debug *ar, Closure *cl) {
  if (noLuaClosure(cl)) {
    ar->source = "=[C]";
    ar->linedefined = -1;
    ar->lastlinedefined = -1;
    ar->what = "C";
  }
  else {
    Proto *p = cl->l.p;
    ar->source = p->source ? getstr(p->source) : "=?";
    ar->linedefined = p->linedefined;
    ar->lastlinedefined = p->lastlinedefined;
    ar->what = (ar->linedefined == 0) ? "main" : "Lua";
  }
  luaO_chunkid(ar->short_src, ar->source, LUA_IDSIZE);
}


/**
 * f 不是Lua函数栈顶压入 nil, 否则在栈顶压入一个 table, 并置
 * table[line] = true, line 为函数所占的所有行号
 */
static void collectvalidlines (lua_State *L, Closure *f) {
  if (noLuaClosure(f)) {
    setnilvalue(L->top);
    api_incr_top(L);
  }
  else {
    int i;
    TValue v;
    int *lineinfo = f->l.p->lineinfo;
    Table *t = luaH_new(L);  /* new table to store active lines */
    sethvalue(L, L->top, t);  /* push it on stack */
    api_incr_top(L);
    setbvalue(&v, 1);  /* boolean 'true' to be the value of all indices */
    for (i = 0; i < f->l.p->sizelineinfo; i++)  /* for all lines with code */
      luaH_setint(L, t, lineinfo[i], &v);  /* table[line] = true */
  }
}


static int auxgetinfo (lua_State *L, const char *what, lua_Debug *ar,
                       Closure *f, CallInfo *ci) {
  int status = 1;
  for (; *what; what++) {
    switch (*what) {
      case 'S': {
        funcinfo(ar, f);
        break;
      }
      case 'l': {
        ar->currentline = (ci && isLua(ci)) ? currentline(ci) : -1;
        break;
      }
      case 'u': {
        ar->nups = (f == NULL) ? 0 : f->c.nupvalues;
        if (noLuaClosure(f)) {
          ar->isvararg = 1;
          ar->nparams = 0;
        }
        else {
          ar->isvararg = f->l.p->is_vararg;
          ar->nparams = f->l.p->numparams;
        }
        break;
      }
      case 't': {
        ar->istailcall = (ci) ? ci->callstatus & CIST_TAIL : 0;
        break;
      }
      case 'n': {
        /* calling function is a known Lua function? */
        if (ci && !(ci->callstatus & CIST_TAIL) && isLua(ci->previous))
          ar->namewhat = getfuncname(L, ci->previous, &ar->name);
        else
          ar->namewhat = NULL;
        if (ar->namewhat == NULL) {
          ar->namewhat = "";  /* not found */
          ar->name = NULL;
        }
        break;
      }
      case 'L':
      case 'f':  /* handled by lua_getinfo */
        break;
      default: status = 0;  /* invalid option */
    }
  }
  return status;
}


/**
 * debug.getinfo ([thread,] f [, what])
 *
 * Returns a table with information about a function. You can give 
 * the function directly or you can give a number as the value of f, 
 * which means the function running at level f of the call stack of the 
 * given thread: level 0 is the current function (getinfo itself); level 
 * 1 is the function that called getinfo (except for tail calls, which do 
 * not count on the stack); and so on. If f is a number larger than the 
 * number of active functions, then getinfo returns nil.
 *
 * The returned table can contain all the fields returned by lua_getinfo, 
 * with the string what describing which fields to fill in. The default 
 * for what is to get all information available, except the table of 
 * valid lines. If present, the option 'f' adds a field named func with 
 * the function itself. If present, the option 'L' adds a field named 
 * activelines with the table of valid lines.
 *
 * For instance, the expression debug.getinfo(1,"n").name returns a 
 * table with a name for the current function, if a reasonable name can
 * be found, and the expression debug.getinfo(print) returns a table with 
 * all available information about the print function.
 */
LUA_API int lua_getinfo (lua_State *L, const char *what, lua_Debug *ar) {
  int status;
  Closure *cl;
  CallInfo *ci;
  StkId func;
  lua_lock(L);
  if (*what == '>') {
    ci = NULL;
    func = L->top - 1;
    api_check(ttisfunction(func), "function expected");
    what++;  /* skip the '>' */
    L->top--;  /* pop function */
  }
  else {
    ci = ar->i_ci;
    func = ci->func;
    lua_assert(ttisfunction(ci->func));
  }
  cl = ttisclosure(func) ? clvalue(func) : NULL;
  status = auxgetinfo(L, what, ar, cl, ci);
  if (strchr(what, 'f')) {
    setobjs2s(L, L->top, func);
    api_incr_top(L);
  }
  if (strchr(what, 'L'))
    collectvalidlines(L, cl);
  lua_unlock(L);
  return status;
}


/*
** {======================================================
** Symbolic Execution
** =======================================================
*/

static const char *getobjname (Proto *p, int lastpc, int reg,
                               const char **name);


/*
** find a "name" for the RK value 'c'
*/
static void kname (Proto *p, int pc, int c, const char **name) {
  if (ISK(c)) {  /* is 'c' a constant? */
    TValue *kvalue = &p->k[INDEXK(c)];
    if (ttisstring(kvalue)) {  /* literal constant? */
      *name = svalue(kvalue);  /* it is its own name */
      return;
    }
    /* else no reasonable name found */
  }
  else {  /* 'c' is a register */
    const char *what = getobjname(p, pc, c, name); /* search for 'c' */
    if (what && *what == 'c') {  /* found a constant name? */
      return;  /* 'name' already filled */
    }
    /* else no reasonable name found */
  }
  *name = "?";  /* no reasonable name found */
}


static int filterpc (int pc, int jmptarget) {
  if (pc < jmptarget)  /* is code conditional (inside a jump)? */
    return -1;  /* cannot know who sets that register */
  else return pc;  /* current position sets that register */
}


/*
** try to find last instruction before 'lastpc' that modified register 'reg'
*/
static int findsetreg (Proto *p, int lastpc, int reg) {
  int pc;
  int setreg = -1;  /* keep last instruction that changed 'reg' */
  int jmptarget = 0;  /* any code before this address is conditional */
  for (pc = 0; pc < lastpc; pc++) {
    Instruction i = p->code[pc];
    OpCode op = GET_OPCODE(i);
    int a = GETARG_A(i);
    switch (op) {
      case OP_LOADNIL: {
        int b = GETARG_B(i);
        if (a <= reg && reg <= a + b)  /* set registers from 'a' to 'a+b' */
          setreg = filterpc(pc, jmptarget);
        break;
      }
      case OP_TFORCALL: {
        if (reg >= a + 2)  /* affect all regs above its base */
          setreg = filterpc(pc, jmptarget);
        break;
      }
      case OP_CALL:
      case OP_TAILCALL: {
        if (reg >= a)  /* affect all registers above base */
          setreg = filterpc(pc, jmptarget);
        break;
      }
      case OP_JMP: {
        int b = GETARG_sBx(i);
        int dest = pc + 1 + b;
        /* jump is forward and do not skip 'lastpc'? */
        if (pc < dest && dest <= lastpc) {
          if (dest > jmptarget)
            jmptarget = dest;  /* update 'jmptarget' */
        }
        break;
      }
      default:
        if (testAMode(op) && reg == a)  /* any instruction that set A */
          setreg = filterpc(pc, jmptarget);
        break;
    }
  }
  return setreg;
}


static const char *getobjname (Proto *p, int lastpc, int reg,
                               const char **name) {
  int pc;
  *name = luaF_getlocalname(p, reg + 1, lastpc);
  if (*name)  /* is a local? */
    return "local";
  /* else try symbolic execution */
  pc = findsetreg(p, lastpc, reg);
  if (pc != -1) {  /* could find instruction? */
    Instruction i = p->code[pc];
    OpCode op = GET_OPCODE(i);
    switch (op) {
      case OP_MOVE: {
        int b = GETARG_B(i);  /* move from 'b' to 'a' */
        if (b < GETARG_A(i))
          return getobjname(p, pc, b, name);  /* get name for 'b' */
        break;
      }
      case OP_GETTABUP:
      case OP_GETTABLE: {
        int k = GETARG_C(i);  /* key index */
        int t = GETARG_B(i);  /* table index */
        const char *vn = (op == OP_GETTABLE)  /* name of indexed variable */
                         ? luaF_getlocalname(p, t + 1, pc)
                         : upvalname(p, t);
        kname(p, pc, k, name);
        return (vn && strcmp(vn, LUA_ENV) == 0) ? "global" : "field";
      }
      case OP_GETUPVAL: {
        *name = upvalname(p, GETARG_B(i));
        return "upvalue";
      }
      case OP_LOADK:
      case OP_LOADKX: {
        int b = (op == OP_LOADK) ? GETARG_Bx(i)
                                 : GETARG_Ax(p->code[pc + 1]);
        if (ttisstring(&p->k[b])) {
          *name = svalue(&p->k[b]);
          return "constant";
        }
        break;
      }
      case OP_SELF: {
        int k = GETARG_C(i);  /* key index */
        kname(p, pc, k, name);
        return "method";
      }
      default: break;  /* go through to return NULL */
    }
  }
  return NULL;  /* could not find reasonable name */
}


static const char *getfuncname (lua_State *L, CallInfo *ci, const char **name) {
  TMS tm = (TMS)0;  /* to avoid warnings */
  Proto *p = ci_func(ci)->p;  /* calling function */
  int pc = currentpc(ci);  /* calling instruction index */
  Instruction i = p->code[pc];  /* calling instruction */
  if (ci->callstatus & CIST_HOOKED) {  /* was it called inside a hook? */
    *name = "?";
    return "hook";
  }
  switch (GET_OPCODE(i)) {
    case OP_CALL:
    case OP_TAILCALL:  /* get function name */
      return getobjname(p, pc, GETARG_A(i), name);
    case OP_TFORCALL: {  /* for iterator */
      *name = "for iterator";
       return "for iterator";
    }
    /* all other instructions can call only through metamethods */
    case OP_SELF: case OP_GETTABUP: case OP_GETTABLE:
      tm = TM_INDEX;
      break;
    case OP_SETTABUP: case OP_SETTABLE:
      tm = TM_NEWINDEX;
      break;
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_MOD:
    case OP_POW: case OP_DIV: case OP_IDIV: case OP_BAND:
    case OP_BOR: case OP_BXOR: case OP_SHL: case OP_SHR: {
      int offset = cast_int(GET_OPCODE(i)) - cast_int(OP_ADD);  /* ORDER OP */
      tm = cast(TMS, offset + cast_int(TM_ADD));  /* ORDER TM */
      break;
    }
    case OP_UNM: tm = TM_UNM; break;
    case OP_BNOT: tm = TM_BNOT; break;
    case OP_LEN: tm = TM_LEN; break;
    case OP_CONCAT: tm = TM_CONCAT; break;
    case OP_EQ: tm = TM_EQ; break;
    case OP_LT: tm = TM_LT; break;
    case OP_LE: tm = TM_LE; break;
    default: lua_assert(0);  /* other instructions cannot call a function */
  }
  *name = getstr(G(L)->tmname[tm]);
  return "metamethod";
}

/* }====================================================== */



/*
** The subtraction of two potentially unrelated pointers is
** not ISO C, but it should not crash a program; the subsequent
** checks are ISO C and ensure a correct result.
*/
static int isinstack (CallInfo *ci, const TValue *o) {
  ptrdiff_t i = o - ci->u.l.base;
  return (0 <= i && i < (ci->top - ci->u.l.base) && ci->u.l.base + i == o);
}


/*
** Checks whether value 'o' came from an upvalue. (That can only happen
** with instructions OP_GETTABUP/OP_SETTABUP, which operate directly on
** upvalues.)
*/
static const char *getupvalname (CallInfo *ci, const TValue *o,
                                 const char **name) {
  LClosure *c = ci_func(ci);
  int i;
  for (i = 0; i < c->nupvalues; i++) {
    if (c->upvals[i]->v == o) {
      *name = upvalname(c->p, i);
      return "upvalue";
    }
  }
  return NULL;
}


/**
 * 得到对象 o 的类型名 和 其相应的变量名, 结果放在栈顶
 */
static const char *varinfo (lua_State *L, const TValue *o) {
  const char *name = NULL;  /* to avoid warnings */
  CallInfo *ci = L->ci;
  const char *kind = NULL;
  if (isLua(ci)) {
    kind = getupvalname(ci, o, &name);  /* check whether 'o' is an upvalue */
    if (!kind && isinstack(ci, o))  /* no? try a register */
      kind = getobjname(ci_func(ci)->p, currentpc(ci),
                        cast_int(o - ci->u.l.base), &name);
  }
  return (kind) ? luaO_pushfstring(L, " (%s '%s')", kind, name) : "";
}


l_noret luaG_typeerror (lua_State *L, const TValue *o, const char *op) {
  const char *t = objtypename(o);
  luaG_runerror(L, "attempt to %s a %s value%s", op, t, varinfo(L, o));
}


l_noret luaG_concaterror (lua_State *L, const TValue *p1, const TValue *p2) {
  if (ttisstring(p1) || cvt2str(p1)) p1 = p2;
  luaG_typeerror(L, p1, "concatenate");
}


l_noret luaG_opinterror (lua_State *L, const TValue *p1,
                         const TValue *p2, const char *msg) {
  lua_Number temp;
  if (!tonumber(p1, &temp))  /* first operand is wrong? */
    p2 = p1;  /* now second is wrong */
  luaG_typeerror(L, p2, msg);
}


/*
** Error when both values are convertible to numbers, but not to integers
*/
l_noret luaG_tointerror (lua_State *L, const TValue *p1, const TValue *p2) {
  lua_Integer temp;
  if (!tointeger(p1, &temp))
    p2 = p1;
  luaG_runerror(L, "number%s has no integer representation", varinfo(L, p2));
}


l_noret luaG_ordererror (lua_State *L, const TValue *p1, const TValue *p2) {
  const char *t1 = objtypename(p1);
  const char *t2 = objtypename(p2);
  if (t1 == t2)
    luaG_runerror(L, "attempt to compare two %s values", t1);
  else
    luaG_runerror(L, "attempt to compare %s with %s", t1, t2);
}


/**
 * 添加错误描述信息, 放在栈顶
 */
static void addinfo (lua_State *L, const char *msg) {
  CallInfo *ci = L->ci;
  if (isLua(ci)) {  /* is Lua code? */
    char buff[LUA_IDSIZE];  /* add file:line information */
    int line = currentline(ci);
    TString *src = ci_func(ci)->p->source;
    if (src)
      luaO_chunkid(buff, getstr(src), LUA_IDSIZE);
    else {  /* no source available; use "?" instead */
      buff[0] = '?'; buff[1] = '\0';
    }
    luaO_pushfstring(L, "%s:%d: %s", buff, line, msg);
  }
}


/**
 * 错误描述信息已经存在栈顶了
 */
l_noret luaG_errormsg (lua_State *L) {
  if (L->errfunc != 0) {  /* is there an error handling function? */
    StkId errfunc = restorestack(L, L->errfunc);
    setobjs2s(L, L->top, L->top - 1);  /* move argument */
    setobjs2s(L, L->top - 1, errfunc);  /* push function */
    L->top++;  /* assume EXTRA_STACK */
    luaD_call(L, L->top - 2, 1, 0);  /* call it */
  }
  luaD_throw(L, LUA_ERRRUN);
}


/**
 * 报运行时错, 添加详细的错误描述信息, 比如出错函数, 文件名, 行号等
 */
l_noret luaG_runerror (lua_State *L, const char *fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  addinfo(L, luaO_pushvfstring(L, fmt, argp));
  va_end(argp);
  luaG_errormsg(L);
}


void luaG_traceexec (lua_State *L) {
  CallInfo *ci = L->ci;
  lu_byte mask = L->hookmask;
  int counthook = ((mask & LUA_MASKCOUNT) && L->hookcount == 0);
  if (counthook)
    resethookcount(L);  /* reset count */
  if (ci->callstatus & CIST_HOOKYIELD) {  /* called hook last time? */
    ci->callstatus &= ~CIST_HOOKYIELD;  /* erase mark */
    return;  /* do not call hook again (VM yielded, so it did not move) */
  }
  if (counthook)
    luaD_hook(L, LUA_HOOKCOUNT, -1);  /* call count hook */
  if (mask & LUA_MASKLINE) {
    Proto *p = ci_func(ci)->p;
    int npc = pcRel(ci->u.l.savedpc, p);
    int newline = getfuncline(p, npc);
    if (npc == 0 ||  /* call linehook when enter a new function, */
        ci->u.l.savedpc <= L->oldpc ||  /* when jump back (loop), or when */
        newline != getfuncline(p, pcRel(L->oldpc, p)))  /* enter a new line */
      luaD_hook(L, LUA_HOOKLINE, newline);  /* call line hook */
  }
  L->oldpc = ci->u.l.savedpc;
  if (L->status == LUA_YIELD) {  /* did hook yield? */
    if (counthook)
      L->hookcount = 1;  /* undo decrement to zero */
    ci->u.l.savedpc--;  /* undo increment (resume will increment it again) */
    ci->callstatus |= CIST_HOOKYIELD;  /* mark that it yielded */
    ci->func = L->top - 1;  /* protect stack below results */
    luaD_throw(L, LUA_YIELD);
  }
}

