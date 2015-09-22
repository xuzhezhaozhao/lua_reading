/*
** $Id: lstate.h,v 2.119 2014/10/30 18:53:28 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#ifndef lstate_h
#define lstate_h

#include "lua.h"

#include "lobject.h"
#include "ltm.h"
#include "lzio.h"


/*

** Some notes about garbage-collected objects: All objects in Lua must
** be kept somehow accessible until being freed, so all objects always
** belong to one (and only one) of these lists, using field 'next' of
** the 'CommonHeader' for the link:
**
** 'allgc': all objects not marked for finalization;
** 'finobj': all objects marked for finalization;
** 'tobefnz': all objects ready to be finalized; 
** 'fixedgc': all objects that are not to be collected (currently
** only small strings, such as reserved words).

*/


struct lua_longjmp;  /* defined in ldo.c */



/* extra stack space to handle TM calls and some other extras */
#define EXTRA_STACK   5


#define BASIC_STACK_SIZE        (2*LUA_MINSTACK)


/* kinds of Garbage Collection */
#define KGC_NORMAL	0
#define KGC_EMERGENCY	1	/* gc was forced by an allocation failure */


/**
 * Lua用一张hash table来管理所有的字符串资源
 */
typedef struct stringtable {
  /* 指向字符串hash表 */
  TString **hash;
  int nuse;  /* number of elements */
  /* hash table 大小 */
  int size;
} stringtable;


/*
** Information about a call.
** When a thread yields, 'func' is adjusted to pretend that the
** top function has only the yielded values in its stack; in that
** case, the actual 'func' value is saved in field 'extra'. 
** When a function calls another with a continuation, 'extra' keeps
** the function index so that, in case of errors, the continuation
** function can be called with the correct top.
*/
typedef struct CallInfo {
  /* only for C light function or C closure */
  StkId func;  /* function index in the stack */
  /* 这个值标识的是当前函数可用的最大栈位置, L->top 超过这个值就是溢出了 */
  StkId	top;  /* top for this function */
  struct CallInfo *previous, *next;  /* dynamic call link */
  union {
    struct {  /* only for Lua functions */
      /* 当前函数的第一个固定(非变长)参数位置 */
      StkId base;  /* base for this function */
	  /* 代码指令执行点, 类似 PC 寄存器 */
      const Instruction *savedpc;
    } l;
    struct {  /* only for C functions */
      lua_KFunction k;  /* continuation in case of yields */
      ptrdiff_t old_errfunc;
      lua_KContext ctx;  /* context info. in case of yields */
    } c;
  } u;
  ptrdiff_t extra;
  short nresults;  /* expected number of results from this function */
  /* 下面就是状态类型宏定义 */
  lu_byte callstatus;
} CallInfo;


/*
** Bits in CallInfo status
*/
#define CIST_OAH	(1<<0)	/* original value of 'allowhook' */
#define CIST_LUA	(1<<1)	/* call is running a Lua function */
#define CIST_HOOKED	(1<<2)	/* call is running a debug hook */
#define CIST_REENTRY	(1<<3)	/* call is running on same invocation of
                                   luaV_execute of previous call */
#define CIST_YPCALL	(1<<4)	/* call is a yieldable protected call */
#define CIST_TAIL	(1<<5)	/* call was tail called */
#define CIST_HOOKYIELD	(1<<6)	/* last hook called yielded */

/* ci: CallInfo */
#define isLua(ci)	((ci)->callstatus & CIST_LUA)

/* assume that CIST_OAH has offset 0 and that 'v' is strictly 0/1 */
#define setoah(st,v)	((st) = ((st) & ~CIST_OAH) | (v))
#define getoah(st)	((st) & CIST_OAH)


/*
** 'global state', shared by all threads of this state
*/
typedef struct global_State {
  /* 全局使用的内存分配器, 在 lua_auxilib.c 中提供了一个示例: l_alloc */
  lua_Alloc frealloc;  /* function to reallocate memory */
  /* frealloc 函数的第一个参数, 用来实现定制内存分配器 */
  void *ud;         /* auxiliary data to 'frealloc' */
  /* 初始为 LG 结构大小 */
  lu_mem totalbytes;  /* number of bytes currently allocated - GCdebt */
  /* 初始为 0 */
  l_mem GCdebt;  /* bytes allocated not yet compensated by the collector */
  /* 单位是字节 */
  lu_mem GCmemtrav;  /* memory traversed by the GC */
  lu_mem GCestimate;  /* an estimate of the non-garbage memory in use */
  /* 初始时其大小为 64 */
  stringtable strt;  /* hash table for strings */
  /* see http://www.lua.org/manual/5.3/manual.html#4.5 about registry*/
  /* 
   * 全局变量表. 
   * 初始时这张表的大小为 2, 只有 array part, 
   * 通过 key LUA_RIDX_MAINTHREAD 和 LUA_RIDX_GLOBALS 来索引, 并且初始化:
   * l_registry[ LUA_RIDX_MAINTHREAD ] = mainthread
   * l_registry[ LUA_RIDX_GLOBALS ] = table of globals, 初始时 global table
   * 是空的.
   */
  TValue l_registry;
  /* 随机数种子, lstate.c 中的 makeseed 函数生成 */
  unsigned int seed;  /* randomized seed for hashes */
  /* 当前 gc 使用的白色种类 */
  lu_byte currentwhite;
  /* 初始为 GCpause */
  lu_byte gcstate;  /* state of garbage collector */
  /* 初始为 KGC_NORMAL */
  lu_byte gckind;  /* kind of GC running */
  /* 初始化 lua_state 的过程中为 0,  初始化结束后置为 1 */
  lu_byte gcrunning;  /* true if GC is running */
  /* 下面几个链表初始都为 NULL */
  GCObject *allgc;  /* list of all collectable objects */
  GCObject **sweepgc;  /* current position of sweep in list */
  GCObject *finobj;  /* list of collectable objects with finalizers */
  GCObject *gray;  /* list of gray objects */
  GCObject *grayagain;  /* list of objects to be traversed atomically */
  GCObject *weak;  /* list of tables with weak values */
  GCObject *ephemeron;  /* list of ephemeron tables (weak keys) */
  GCObject *allweak;  /* list of all-weak tables */
  /* list of being-finalized */
  GCObject *tobefnz;  /* list of userdata to be GC */
  /* 永远不回收的对象链表, 如保留关键字的字符串, 对象必须在创建之后马上
   * 从 allgc 链表移入该链表中, 用的是 lgc.c 中的 luaC_fix 函数 */
  GCObject *fixedgc;  /* list of objects not to be collected */
  struct lua_State *twups;  /* list of threads with open upvalues */
  Mbuffer buff;  /* temporary buffer for string concatenation */
  /* 初始为 0 */
  unsigned int gcfinnum;  /* number of finalizers to call in each GC step */
  /* 初始为 LUAI_GCPAUSE (200) */
  int gcpause;  /* size of pause between successive GCs */
  /* 初始为 LUAI_GCMUL (200) */
  int gcstepmul;  /* GC 'granularity' */
  /* 初始化成功完成后其值被设置为 lauxlib.c 中的 panic 函数 */
  lua_CFunction panic;  /* to be called in unprotected errors */
  /* 程序启动的主线程, 就是与其一起被创建的那个线程 */
  struct lua_State *mainthread;
  /* 初始化完成之前都是 NULL, 初始化完成取得正确的值 */
  const lua_Number *version;  /* pointer to version number */
  /* 初始为 "not enough memory" 该字符串永远不会被回收 */
  TString *memerrmsg;  /* memory-error message */
  /* 初始化为元方法字符串, 在 ltm.c luaT_init 中, 且将它们标记为不可回收对象 */
  TString *tmname[TM_N];  /* array with tag-method names */
  /* 初始时全部为 NULL */
  struct Table *mt[LUA_NUMTAGS];  /* metatables for basic types */
} global_State;


/*
** 'per thread' state
*/
struct lua_State {
  CommonHeader;
  /* 初始为 LUA_OK */
  lu_byte status;
  /* 初始时指向栈第 2 个位置  */
  StkId top;  /* first free slot in the stack */
  /* 初始时指向与 mainthread 一起创建的 global_state */
  global_State *l_G;
  /* 初始时指向 L->base_ci, L 为其自身 */
  /* 请再看下面 base_ci 域的描述 */
  CallInfo *ci;  /* call info for current function */
  const Instruction *oldpc;  /* last pc traced */
  /* 在这后面还有 EXTRA_STACK 个位置大小作为预留空间 */
  /* 初始时指向 L->stack + L->stacksize - EXTRA_STACK */
  StkId stack_last;  /* last free slot in the stack */
  /* 栈初始大小为 40, 栈上元素全部为 nil */
  StkId stack;  /* stack base */
  /* 初始为 NULL */
  UpVal *openupval;  /* list of open upvalues in this stack */
  GCObject *gclist;
  /* 初始时指向自身, 表示空链表 */
  struct lua_State *twups;  /* list of threads with open upvalues */
  struct lua_longjmp *errorJmp;  /* current error recover point */
  /* 调用链的头部 */
  /*
   * 初始时其 next, previous 域都为 NULL, 其 func 域 且 ci 指针指向它, 
   * 其 func 域指向栈第一个位置, 且初始该位置为 nil, 其 top 域指向第 22
   * 个位置, 即栈为函数调用留有 20 个位置可以使用, 其 callstatus 域为 0.
   * 此时 L->top 指向第 2 个位置, 因为第 1 个位置已经由 func 域占用了.
   */
  CallInfo base_ci;  /* CallInfo for first level (C calling Lua) */
  lua_Hook hook;
  /* 初始为 0 */
  ptrdiff_t errfunc;  /* current error handling function (stack index) */
  /* 栈初始大小为 40, 栈上元素全部为 nil */
  int stacksize;
  /* 初始为 0 */
  int basehookcount;
  /* 初始为 0 */
  int hookcount;
  /* 初始为 1, TODO */
  unsigned short nny;  /* number of non-yieldable calls in stack */
  /* 初始为 0 */
  unsigned short nCcalls;  /* number of nested C calls */
  /* 初始为 0 */
  lu_byte hookmask;
  /* 初始为 1 */
  lu_byte allowhook;
};


/* globale state */
#define G(L)	(L->l_G)


/*
** Union of all collectable objects (only for conversions)
*/
union GCUnion {
  GCObject gc;  /* common header */
  struct TString ts;
  struct Udata u;
  union Closure cl;
  struct Table h;
  struct Proto p;
  struct lua_State th;  /* thread */
};


#define cast_u(o)	cast(union GCUnion *, (o))

/* macros to convert a GCObject into a specific value */
/* TString */
#define gco2ts(o)  \
	check_exp(novariant((o)->tt) == LUA_TSTRING, &((cast_u(o))->ts))
/* user data */
#define gco2u(o)  check_exp((o)->tt == LUA_TUSERDATA, &((cast_u(o))->u))
/* Lua closure */
#define gco2lcl(o)  check_exp((o)->tt == LUA_TLCL, &((cast_u(o))->cl.l))
/* C closure */
#define gco2ccl(o)  check_exp((o)->tt == LUA_TCCL, &((cast_u(o))->cl.c))
#define gco2cl(o)  \
	check_exp(novariant((o)->tt) == LUA_TFUNCTION, &((cast_u(o))->cl))
#define gco2t(o)  check_exp((o)->tt == LUA_TTABLE, &((cast_u(o))->h))
#define gco2p(o)  check_exp((o)->tt == LUA_TPROTO, &((cast_u(o))->p))
#define gco2th(o)  check_exp((o)->tt == LUA_TTHREAD, &((cast_u(o))->th))


/* macro to convert a Lua object into a GCObject */
/*
 * 返回指向 GCObject 的指针
 */
#define obj2gco(v) \
	check_exp(novariant((v)->tt) < LUA_TDEADKEY, (&(cast_u(v)->gc)))


/* actual number of total bytes allocated */
#define gettotalbytes(g)	((g)->totalbytes + (g)->GCdebt)

LUAI_FUNC void luaE_setdebt (global_State *g, l_mem debt);
LUAI_FUNC void luaE_freethread (lua_State *L, lua_State *L1);
LUAI_FUNC CallInfo *luaE_extendCI (lua_State *L);
LUAI_FUNC void luaE_freeCI (lua_State *L);
LUAI_FUNC void luaE_shrinkCI (lua_State *L);


#endif

