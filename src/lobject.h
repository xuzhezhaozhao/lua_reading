/*
** $Id: lobject.h,v 2.106 2015/01/05 13:52:37 roberto Exp $
** Type definitions for Lua objects
** See Copyright Notice in lua.h
*/


#ifndef lobject_h
#define lobject_h


#include <stdarg.h>


#include "llimits.h"
#include "lua.h"


/*
** Extra tags for non-values
*/
#define LUA_TPROTO	LUA_NUMTAGS
#define LUA_TDEADKEY	(LUA_NUMTAGS+1)

/*
** number of all possible tags (including LUA_TNONE but excluding DEADKEY)
*/
#define LUA_TOTALTAGS	(LUA_TPROTO + 2)


/*
** tags for Tagged Values have the following use of bits:
** bits 0-3: actual tag (a LUA_T* value)
** bits 4-5: variant bits
** bit 6: whether value is collectable
*/

#define VARBITS		(3 << 4)


/*
** LUA_TFUNCTION variants:
** 0 - Lua function
** 1 - light C function
** 2 - regular C function (closure)
*/

/* Variant tags for functions */
#define LUA_TLCL	(LUA_TFUNCTION | (0 << 4))  /* Lua closure */
#define LUA_TLCF	(LUA_TFUNCTION | (1 << 4))  /* light C function */
#define LUA_TCCL	(LUA_TFUNCTION | (2 << 4))  /* C closure */


/* 低三位是 non-varant tag, 标识的是基本类型 */

/* Variant tags for strings */
#define LUA_TSHRSTR	(LUA_TSTRING | (0 << 4))  /* short strings */
#define LUA_TLNGSTR	(LUA_TSTRING | (1 << 4))  /* long strings */


/* Variant tags for numbers */
#define LUA_TNUMFLT	(LUA_TNUMBER | (0 << 4))  /* float numbers */
#define LUA_TNUMINT	(LUA_TNUMBER | (1 << 4))  /* integer numbers */


/* Bit mark for collectable types */
#define BIT_ISCOLLECTABLE	(1 << 6)

/* mark a tag as collectable */
#define ctb(t)			((t) | BIT_ISCOLLECTABLE)


/*
** Common type for all collectable objects
*/
typedef struct GCObject GCObject;


/*
** Common Header for all collectable objects (in macro form, to be
** included in other objects)
*/
/**
 * next: 下一个对象
 * tt: 对象的类型
 * marked: 对象的状态标记，是否可被回收, 是否是 dead
 */
#define CommonHeader	GCObject *next; lu_byte tt; lu_byte marked


/*
** Common type has only the common header
*/
struct GCObject {
  CommonHeader;
};



/*
** Union of all Lua values
*/
typedef union Value Value;




/*
** Tagged Values. This is the basic representation of values in Lua,
** an actual value plus a tag with its type.
*/

#define TValuefields	Value value_; int tt_

typedef struct lua_TValue TValue;


/* macro defining a nil value */
#define NILCONSTANT	{NULL}, LUA_TNIL


#define val_(o)		((o)->value_)


/* raw type tag of a TValue */
#define rttype(o)	((o)->tt_)

/* tag with no variants (bits 0-3), 基本类型的标识 */
/* non-variant 取bits 4 - 5, 同一基本类型的演化， 如整数，浮点数，长短字符串 */
/* 只取低三位 */
#define novariant(x)	((x) & 0x0F)

/* type tag of a TValue (bits 0-3 for tags + variant bits 4-5) */
#define ttype(o)	(rttype(o) & 0x3F)

/* type tag of a TValue with no variants (bits 0-3) */
#define ttnov(o)	(novariant(rttype(o)))


/* Macros to test type */
#define checktag(o,t)		(rttype(o) == (t))
#define checktype(o,t)		(ttnov(o) == (t))
#define ttisnumber(o)		checktype((o), LUA_TNUMBER)
#define ttisfloat(o)		checktag((o), LUA_TNUMFLT)
#define ttisinteger(o)		checktag((o), LUA_TNUMINT)
#define ttisnil(o)		checktag((o), LUA_TNIL)
#define ttisboolean(o)		checktag((o), LUA_TBOOLEAN)
#define ttislightuserdata(o)	checktag((o), LUA_TLIGHTUSERDATA)
#define ttisstring(o)		checktype((o), LUA_TSTRING)
#define ttisshrstring(o)	checktag((o), ctb(LUA_TSHRSTR))
#define ttislngstring(o)	checktag((o), ctb(LUA_TLNGSTR))
#define ttistable(o)		checktag((o), ctb(LUA_TTABLE))
#define ttisfunction(o)		checktype(o, LUA_TFUNCTION)
#define ttisclosure(o)		((rttype(o) & 0x1F) == LUA_TFUNCTION)
#define ttisCclosure(o)		checktag((o), ctb(LUA_TCCL))
#define ttisLclosure(o)		checktag((o), ctb(LUA_TLCL))
#define ttislcf(o)		checktag((o), LUA_TLCF)
#define ttisfulluserdata(o)	checktag((o), ctb(LUA_TUSERDATA))
#define ttisthread(o)		checktag((o), ctb(LUA_TTHREAD))
#define ttisdeadkey(o)		checktag((o), LUA_TDEADKEY)


/* Macros to access values */
/* 获取 Value 的 lua_Integer 值 */
#define ivalue(o)	check_exp(ttisinteger(o), val_(o).i)
/* 获取 Value 的 lua_Number 值, 浮点数 */
#define fltvalue(o)	check_exp(ttisfloat(o), val_(o).n)
#define nvalue(o)	check_exp(ttisnumber(o), \
	(ttisinteger(o) ? cast_num(ivalue(o)) : fltvalue(o)))
/* 获取 GCObject 指针 */
#define gcvalue(o)	check_exp(iscollectable(o), val_(o).gc)
/* 获取 light userdata 指针 */
#define pvalue(o)	check_exp(ttislightuserdata(o), val_(o).p)
/* 将指向GCObject的o转为指向TString的指针 */
#define tsvalue(o)	check_exp(ttisstring(o), gco2ts(val_(o).gc))
/* o转为指向Udata的指针 */
#define uvalue(o)	check_exp(ttisfulluserdata(o), gco2u(val_(o).gc))
#define clvalue(o)	check_exp(ttisclosure(o), gco2cl(val_(o).gc))
/* lua closure */
#define clLvalue(o)	check_exp(ttisLclosure(o), gco2lcl(val_(o).gc))
/* o转为指向 Closure 的指针 */
#define clCvalue(o)	check_exp(ttisCclosure(o), gco2ccl(val_(o).gc))
/* o转为指向lua_CFunction的指针 */
#define fvalue(o)	check_exp(ttislcf(o), val_(o).f)
/* o转为指向Table的指针 */
#define hvalue(o)	check_exp(ttistable(o), gco2t(val_(o).gc))
#define bvalue(o)	check_exp(ttisboolean(o), val_(o).b)
#define thvalue(o)	check_exp(ttisthread(o), gco2th(val_(o).gc))
/* a dead value may get the 'gc' field, but cannot access its contents */
#define deadvalue(o)	check_exp(ttisdeadkey(o), cast(void *, val_(o).gc))

/**
 * lua中只有 nil 和 false 为 false, 0 也是 true
 */
#define l_isfalse(o)	(ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))


/* 对象 o 是否可回收 */
#define iscollectable(o)	(rttype(o) & BIT_ISCOLLECTABLE)


/* Macros for internal tests */
#define righttt(obj)		(ttype(obj) == gcvalue(obj)->tt)

/**
 * assert: obj 指向的对象还活着
 */
#define checkliveness(g,obj) \
	lua_longassert(!iscollectable(obj) || \
			(righttt(obj) && !isdead(g,gcvalue(obj))))


/* Macros to set values */
#define settt_(o,t)	((o)->tt_=(t))

#define setfltvalue(obj,x) \
  { TValue *io=(obj); val_(io).n=(x); settt_(io, LUA_TNUMFLT); }

/* 将 obj 对象设为 int 类型，值为 x */
#define setivalue(obj,x) \
  { TValue *io=(obj); val_(io).i=(x); settt_(io, LUA_TNUMINT); }

/* 将 obj 对象类型设为nil */
#define setnilvalue(obj) settt_(obj, LUA_TNIL)

/**
 * 将 obj 指向对象值设为 x , x 类型为CFunction , 同时更改其类型为 LUA_TLCF
 */
#define setfvalue(obj,x) \
  { TValue *io=(obj); val_(io).f=(x); settt_(io, LUA_TLCF); }

/**
 * x 就是一个指向一片内存区的指针, 即 light user data
 */
#define setpvalue(obj,x) \
  { TValue *io=(obj); val_(io).p=(x); settt_(io, LUA_TLIGHTUSERDATA); }

#define setbvalue(obj,x) \
  { TValue *io=(obj); val_(io).b=(x); settt_(io, LUA_TBOOLEAN); }

/* x 为 GCObject* */
#define setgcovalue(L,obj,x) \
  { TValue *io = (obj); GCObject *i_g=(x); \
    val_(io).gc = i_g; settt_(io, ctb(i_g->tt)); }

/**
 * 将obj指向的对象 Value 的union元素设为 (GCObjec *) 类型，并指向x指向的对象;
 * tag设为 x 指向的对象tag，并添加可回收属性
 */
#define setsvalue(L,obj,x) \
  { TValue *io = (obj); TString *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(x_->tt)); \
    checkliveness(G(L),io); }

/**
 * 将obj指向的对象Value 的union元素设为 (GCObjec *) 类型，并指向x指向的对象;
 * tag设为 x 指向的对象tag，并添加 LUA_TUSERDATA 属性
 */
#define setuvalue(L,obj,x) \
  { TValue *io = (obj); Udata *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TUSERDATA)); \
    checkliveness(G(L),io); }

/**
 * x 为 lua_State * 类型
 */
#define setthvalue(L,obj,x) \
  { TValue *io = (obj); lua_State *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TTHREAD)); \
    checkliveness(G(L),io); }

#define setclLvalue(L,obj,x) \
  { TValue *io = (obj); LClosure *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TLCL)); \
    checkliveness(G(L),io); }

/**
 * x 为 闭包类型, obj 标记为 可回收
 */
#define setclCvalue(L,obj,x) \
  { TValue *io = (obj); CClosure *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TCCL)); \
    checkliveness(G(L),io); }

/* set table */
#define sethvalue(L,obj,x) \
  { TValue *io = (obj); Table *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TTABLE)); \
    checkliveness(G(L),io); }

/* 设置obj类型为 deadkey */
#define setdeadvalue(obj)	settt_(obj, LUA_TDEADKEY)



/* 复制对象，对象 *obj1 = *obj2 */
#define setobj(L,obj1,obj2) \
	{ TValue *io1=(obj1); *io1 = *(obj2); \
	  (void)L; checkliveness(G(L),io1); }


/*
** different types of assignments, according to destination
*/

/* from stack to (same) stack */
#define setobjs2s	setobj
/* to stack (not from same stack) */
#define setobj2s	setobj
/* 设置TString对象 */
#define setsvalue2s	setsvalue
#define sethvalue2s	sethvalue
#define setptvalue2s	setptvalue
/* from table to same table */
#define setobjt2t	setobj
/* to table */
#define setobj2t	setobj
/* to new object */
#define setobj2n	setobj
#define setsvalue2n	setsvalue




/*
** {======================================================
** types and prototypes
** =======================================================
*/


union Value {
  GCObject *gc;    /* collectable objects */
  void *p;         /* light userdata */
  int b;           /* booleans */
  lua_CFunction f; /* light C functions */
  lua_Integer i;   /* integer numbers */
  lua_Number n;    /* float numbers */
};


/**
 * tagged value
 */
struct lua_TValue {
  TValuefields;
};


typedef TValue *StkId;  /* index to stack elements */




/*
** Header for string value; string bytes follow the end of this structure
** (aligned according to 'UTString'; see next).
*/
/**
 * 参考stack overflow: 
 * http://stackoverflow.com/questions/8979999/why-does-internal-lua-strings-store-the-way-they-do
 * 
 * This is a hack, 字符数据跟在结构体之后, 可以类似下面的方法
 * TString *str = (TString*)malloc(sizeof(TString) + <length_of_string>);
 * 这样做的好处的减少了内存碎片, 但一定要小心操作, 最好将对字符串的处理
 * 用宏或内联函数封装起来.
 *
 * lua的实现者为了效率无所不用其极, 为了保证字符串的地址总是对齐(alignment)的,
 * 又引入了一个 UTString 的 union 类型, 所以分配字符串的操作应该是下面这样的,
 * 
 * TString *str = (TString*)malloc(sizeof(UTString) + <length_of_string>);
 *
 */
typedef struct TString {
  CommonHeader;
  /* 
   * long strings 是否计算了 hash 值, 为 0 表示没有计算, 一个 long string 
   * 在创建时都是不计算 hash 值的, 只有在第一次用到(如作为table的key)的时
   * 候才计算 
   * 
   * reserved words 在 llex.c 中, 作为索引位置, 下标从 1 开始
   */
  lu_byte extra;  /* reserved words for short strings; "has hash" for longs */
  /* hash code of string */
  unsigned int hash;
  size_t len;  /* number of characters in string */
  /* 全部字符串是存在一张hash table 中, 解决冲突的方法的链式 */
  struct TString *hnext;  /* linked list for hash table */
} TString;


/*
** Ensures that address after this type is always fully aligned.
*/

/**
 * 
 * 关于上面这句话可以用下面的程序测试， 我们会发现 tt.us 和 tt.i的地址总是对齐
 * 到8. 在TestPadding结构中 us 会占用16个字节的空间，而不是9个。
 * 且sizeof(UTString)的值也是16不是9.
typedef union UTSting {
	L_Umaxalign dummy;
	char a[9];
} UTSting;

typedef struct TestPadding {
	UTSting us;
	char i;
} TestPadding;

int main(int argc, char *argv[])
{
	TestPadding tt;
	printf("%p\n", &tt.us);
	printf("%p\n", &tt.i);

	return 0;
}
*/

typedef union UTString {
  L_Umaxalign dummy;  /* ensures maximum alignment for strings */
  TString tsv;
} UTString;


/*
** Get the actual string (array of bytes) from a 'TString'.
** (Access to 'extra' ensures that value is really a 'TString'.)
*/
/* 注意 const 的区别 */
#define getaddrstr(ts)	(cast(char *, (ts)) + sizeof(UTString))
#define getstr(ts)  \
  check_exp(sizeof((ts)->extra), cast(const char*, getaddrstr(ts)))

/* get the actual string (array of bytes) from a Lua value */
#define svalue(o)       getstr(tsvalue(o))


/*
** Header for userdata; memory area follows the end of this structure
** (aligned according to 'UUdata'; see next).
*/
/**
 * 数据在结构体之后, 与 TString 类似
 */
typedef struct Udata {
  CommonHeader;
  lu_byte ttuv_;  /* user value's tag */
  struct Table *metatable;
  size_t len;  /* number of bytes */
  union Value user_;  /* user value */
} Udata;


/*
** Ensures that address after this type is always fully aligned.
*/
typedef union UUdata {
  L_Umaxalign dummy;  /* ensures maximum alignment for 'local' udata */
  Udata uv;
} UUdata;


/*
**  Get the address of memory block inside 'Udata'.
** (Access to 'ttuv_' ensures that value is really a 'Udata'.)
*/
#define getudatamem(u)  \
  check_exp(sizeof((u)->ttuv_), (cast(char*, (u)) + sizeof(UUdata)))

/**
 * 将 o 指向的对象的值和类型赋给 u 指向的userdata对象
 */
#define setuservalue(L,u,o) \
	{ const TValue *io=(o); Udata *iu = (u); \
	  iu->user_ = io->value_; iu->ttuv_ = io->tt_; \
	  checkliveness(G(L),io); }


/**
 * u 为 Udata 指针，将其对象值赋给 o 所指对象
 */
#define getuservalue(L,u,o) \
	{ TValue *io=(o); const Udata *iu = (u); \
	  io->value_ = iu->user_; io->tt_ = iu->ttuv_; \
	  checkliveness(G(L),io); }


/*
** Description of an upvalue for function prototypes
*/
typedef struct Upvaldesc {
  TString *name;  /* upvalue name (for debug information) */
  lu_byte instack;  /* whether it is in stack */
  lu_byte idx;  /* index of upvalue (in stack or in outer function's list) */
} Upvaldesc;


/*
** Description of a local variable for function prototypes
** (used for debug information)
*/
typedef struct LocVar {
  TString *varname;
  /* 指令相对位置 */
  int startpc;  /* first point where variable is active */
  int endpc;    /* first point where variable is dead */
} LocVar;


/*
** Function Prototypes
*/
typedef struct Proto {
  CommonHeader;
  lu_byte numparams;  /* number of fixed parameters */
  lu_byte is_vararg;
  lu_byte maxstacksize;  /* maximum stack used by this function */
  int sizeupvalues;  /* size of 'upvalues' */
  int sizek;  /* size of 'k' */
  int sizecode;
  int sizelineinfo;
  int sizep;  /* size of 'p' */
  int sizelocvars;
  int linedefined;
  int lastlinedefined;
  TValue *k;  /* constants used by the function */
  /* 函数原型的指令序列, 起始位置 */
  Instruction *code;
  struct Proto **p;  /* functions defined inside the function */
  int *lineinfo;  /* map from opcodes to source lines (debug information) */
  /* 是个数组指针 */
  LocVar *locvars;  /* information about local variables (debug information) */
  /* 是个数组指针 */
  Upvaldesc *upvalues;  /* upvalue information */
  struct LClosure *cache;  /* last created closure with this prototype */
  TString  *source;  /* used for debug information */
  GCObject *gclist;
} Proto;



/*
** Lua Upvalues
*/
typedef struct UpVal UpVal;


/*
** Closures
*/

#define ClosureHeader \
	CommonHeader; lu_byte nupvalues; GCObject *gclist

typedef struct CClosure {
  ClosureHeader;
  lua_CFunction f;
  TValue upvalue[1];  /* list of upvalues */
} CClosure;


typedef struct LClosure {
  ClosureHeader;
  struct Proto *p;
  UpVal *upvals[1];  /* list of upvalues */
} LClosure;


typedef union Closure {
  CClosure c;
  LClosure l;
} Closure;

/* o 对象是否是 lua 函数，闭包 */
#define isLfunction(o)	ttisLclosure(o)

/* lua 闭包的 Proto */
#define getproto(o)	(clLvalue(o)->p)


/*
** Tables
*/
/**
 * key 可以是任意类型，不一定是 string
 */
typedef union TKey {
  struct {
	/*
	 * 在 lgc.c 的 removeentry 函数中有用到, 将其类型(_tt)标记为
	 * LUA_TDEADKEY 就表示该 entry 被删除.
	 */
    TValuefields;
	/** 
	 * 因为 node 数组是个 hash 表，解决冲突办法是链表，但这个链表直接是在
	 * 原 node 数组里面，通过偏移值来链接, next 就是这个偏移值，而不是下一
	 * 个元素的下标, 为0表示链表结束
	 */
    int next;  /* for chaining (offset for next node) */
  } nk;
  /* key 的值 */
  TValue tvk;
} TKey;


/* copy a value into a key without messing up field 'next' */
/**
 * key: 为 Tkey* 类型
 * obj: 为 TValue* 类型
 */
#define setnodekey(L,key,obj) \
	{ TKey *k_=(key); const TValue *io_=(obj); \
	  k_->nk.value_ = io_->value_; k_->nk.tt_ = io_->tt_; \
	  (void)L; checkliveness(G(L),io_); }


typedef struct Node {
  TValue i_val;
  TKey i_key;
} Node;


typedef struct Table {
  CommonHeader;
  /* 初始时这个值为 cast_byte(~0) */
  lu_byte flags;  /* 1<<p means tagmethod(p) is not present */
  /* node array 的大小总是是 2 的整数次方 */
  lu_byte lsizenode;  /* log2 of size of 'node' array */
  unsigned int sizearray;  /* size of 'array' array */
  /* sequence array */
  TValue *array;  /* array part */
  /* node hash table */
  Node *node;
  Node *lastfree;  /* any free position is before this position */
  struct Table *metatable;
  GCObject *gclist;
} Table;



/*
** 'module' operation for hashing (size is always a power of 2)
*/
/* s % size */
#define lmod(s,size) \
	(check_exp((size&(size-1))==0, (cast(int, (s) & ((size)-1)))))


#define twoto(x)	(1<<(x))
/* t 为表，返回 Table 的 node 数组大小 */
#define sizenode(t)	(twoto((t)->lsizenode))


/*
** (address of) a fixed nil value
*/
/**
 * 一个全局变量的地址
 */
#define luaO_nilobject		(&luaO_nilobject_)

/**
 * 在 lobject.c 中定义如下
 *
 * #define NILCONSTANT	{NULL}, LUA_TNIL
 * LUAI_DDEF const TValue luaO_nilobject_ = {NILCONSTANT};
 * 
 */

LUAI_DDEC const TValue luaO_nilobject_;

/* size of buffer for 'luaO_utf8esc' function */
#define UTF8BUFFSZ	8

LUAI_FUNC int luaO_int2fb (unsigned int x);
LUAI_FUNC int luaO_fb2int (int x);
LUAI_FUNC int luaO_utf8esc (char *buff, unsigned long x);
 /* ceil( log2(x) ) */
LUAI_FUNC int luaO_ceillog2 (unsigned int x);
/**
 * 各种运算符实现, 类型不同的时候会尝试将类型都转为浮点数再进行运算, 若类型
 * 没有基本运算符，则会尝试使用 metamethod;
 * op: 运算符类型
 * p1: 第一个操作数
 * p2: 第二个操作数
 * res: 结果
 */
LUAI_FUNC void luaO_arith (lua_State *L, int op, const TValue *p1,
                           const TValue *p2, TValue *res);
/**
 * 字符串转 为 number 类型, 先尝试转为 lua_Integer 类型，
 * 失败后尝试转为lua_Number类型,
 * 
 * 成功则返回字符串长度，失败返回0
 */
LUAI_FUNC size_t luaO_str2num (const char *s, TValue *o);
LUAI_FUNC int luaO_hexavalue (int c);
/* 将 number 对象转成字符串, 如 5， 5.0， 5e9 */
LUAI_FUNC void luaO_tostring (lua_State *L, StkId obj);
/* this function handles only '%d', '%c', '%f', '%p', and '%s' 
   conventional formats, plus Lua-specific '%I' and '%U' */
LUAI_FUNC const char *luaO_pushvfstring (lua_State *L, const char *fmt,
                                                       va_list argp);
LUAI_FUNC const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);
/**
 * 获取源代码描述, 存在 out 中, bufflen 为描述字符最大长度;
 * 调试中很有用
 */
LUAI_FUNC void luaO_chunkid (char *out, const char *source, size_t len);


#endif

