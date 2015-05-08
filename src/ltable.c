/*
** $Id: ltable.c,v 2.100 2015/01/05 13:52:37 roberto Exp $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/

#define ltable_c
#define LUA_CORE

#include "lprefix.h"

#include <stdio.h>

/*
** Implementation of tables (aka arrays, objects, or hash tables).
** Tables keep its elements in two parts: an array part and a hash part.
** Non-negative integer keys are all candidates to be kept in the array
** part. The actual size of the array is the largest 'n' such that at
** least half the slots between 0 and n are in use.
** Hash uses a mix of chained scatter table with Brent's variation.
** A main invariant of these tables is that, if an element is not
** in its main position (i.e. the 'original' position that its hash gives
** to it), then the colliding element is in its own main position.
** Hence even when the load factor reaches 100%, performance remains good.
*/

#include <float.h>
#include <math.h>
#include <string.h>
#include <limits.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lvm.h"


/*
** Maximum size of array part (MAXASIZE) is 2^MAXABITS. MAXABITS is
** the largest integer such that MAXASIZE fits in an unsigned int.
*/
#define MAXABITS	cast_int(sizeof(int) * CHAR_BIT - 1)
#define MAXASIZE	(1u << MAXABITS)

/*
** Maximum size of hash part is 2^MAXHBITS. MAXHBITS is the largest
** integer such that 2^MAXHBITS fits in a signed int. (Note that the
** maximum number of elements in a table, 2^MAXABITS + 2^MAXHBITS, still
** fits comfortably in an unsigned int.)
*/
#define MAXHBITS	(MAXABITS - 1)


/**
 * t 是表，n 为 hash 值, 返回该hash值对应 node 数组中的位置的地址
 */
#define hashpow2(t,n)		(gnode(t, lmod((n), sizenode(t))))

/* 计算 string， boolean，int hash 值对应表中的 position */
#define hashstr(t,str)		hashpow2(t, (str)->hash)
#define hashboolean(t,p)	hashpow2(t, p)
#define hashint(t,i)		hashpow2(t, i)


/*
** for some types, it is better to avoid modulus by power of 2, as
** they tend to have many 2 factors.
*/
/* 比如浮点数 */
#define hashmod(t,n)	(gnode(t, ((n) % ((sizenode(t)-1)|1))))


#define hashpointer(t,p)	hashmod(t, point2int(p))


#define dummynode		(&dummynode_)

#define isdummy(n)		((n) == dummynode)

static const Node dummynode_ = {
  {NILCONSTANT},  /* value */
  {{NILCONSTANT, 0}}  /* key */
};


/*
** Checks whether a float has a value representable as a lua_Integer
** (and does the conversion if so)
*/
/* *p 保存转换后的整数值, 并返回 1, 不能转换则返回 0 */
static int numisinteger (lua_Number x, lua_Integer *p) {
  if ((x) == l_floor(x))  /* integral value? */
    return lua_numbertointeger(x, p);  /* try as an integer */
  else return 0;
}


/*
** hash for floating-point numbers
*/
static Node *hashfloat (const Table *t, lua_Number n) {
  int i;
  n = l_mathop(frexp)(n, &i) * cast_num(INT_MAX - DBL_MAX_EXP);
  i += cast_int(n);
  if (i < 0) {
    if (cast(unsigned int, i) == 0u - i)  /* use unsigned to avoid overflows */
      i = 0;  /* handle INT_MIN */
    i = -i;  /* must be a positive value */
  }
  return hashmod(t, i);
}



/*
** returns the 'main' position of an element in a table (that is, the index
** of its hash value)
*/
/* 获取 key 的 hash 值 在 table 的 node hash 表中的对应的索引值 */
static Node *mainposition (const Table *t, const TValue *key) {
  switch (ttype(key)) {
    case LUA_TNUMINT:
      return hashint(t, ivalue(key));
    case LUA_TNUMFLT:
      return hashfloat(t, fltvalue(key));
    case LUA_TSHRSTR:
      return hashstr(t, tsvalue(key));
    case LUA_TLNGSTR: {
      TString *s = tsvalue(key);
      if (s->extra == 0) {  /* no hash? */
        s->hash = luaS_hash(getstr(s), s->len, s->hash);
        s->extra = 1;  /* now it has its hash */
      }
      return hashstr(t, tsvalue(key));
    }
    case LUA_TBOOLEAN:
      return hashboolean(t, bvalue(key));
    case LUA_TLIGHTUSERDATA:
      return hashpointer(t, pvalue(key));
    case LUA_TLCF:
      return hashpointer(t, fvalue(key));
    default:
      return hashpointer(t, gcvalue(key));
  }
}


/*
** returns the index for 'key' if 'key' is an appropriate key to live in
** the array part of the table, 0 otherwise.
*/
/* key 为正整数且 key 小于array的最大限制长度, 否则返回0 */
static unsigned int arrayindex (const TValue *key) {
  if (ttisinteger(key)) {
    lua_Integer k = ivalue(key);
    if (0 < k && (lua_Unsigned)k <= MAXASIZE)
      return cast(unsigned int, k);  /* 'key' is an appropriate array index */
  }
  return 0;  /* 'key' did not match some condition */
}


/*
** returns the index of a 'key' for table traversals. First goes all
** elements in the array part, then elements in the hash part. The
** beginning of a traversal is signaled by 0.
*/
/**
 * 查找 key 是在 table 中的位置，位置先从 array 数组算起，再是 node hash table,
 * 起始位置是 1.
 * key 为 nil 返回 0，key 若不存在则会报运行时错误
 */
static unsigned int findindex (lua_State *L, Table *t, StkId key) {
  unsigned int i;
  if (ttisnil(key)) return 0;  /* first iteration */
  i = arrayindex(key);
  if (i != 0 && i <= t->sizearray)  /* is 'key' inside array part? */
    return i;  /* yes; that's the index */
  else {
    int nx;
    Node *n = mainposition(t, key);
    for (;;) {  /* check whether 'key' is somewhere in the chain */
      /* key may be dead already, but it is ok to use it in 'next' */
      if (luaV_rawequalobj(gkey(n), key) ||
            (ttisdeadkey(gkey(n)) && iscollectable(key) &&
             deadvalue(gkey(n)) == gcvalue(key))) {
        i = cast_int(n - gnode(t, 0));  /* key index in hash table */
        /* hash elements are numbered after array ones */
        return (i + 1) + t->sizearray;
      }
      nx = gnext(n);
      if (nx == 0)
        luaG_runerror(L, "invalid key to 'next'");  /* key not found */
      else n += nx;
    }
  }
}


/**
 * 首先查找 key 在 table 中的索引顺序位置，查找顺序是先array，再 
 * node hash table, 然后将这个位置后的第一个非 nil 元素的 key, value 
 * 分别存入 key 和 key + 1 两个位置, 对于 array 来说，key 就是数组下标加 1.
 * 
 * key 如果是栈顶位置的话，新压入的 key, value 对就在栈顶两个位置.
 * 
 * 如果找到非 nil 元素，返回 1, 否则返回 0.
 */
int luaH_next (lua_State *L, Table *t, StkId key) {
  unsigned int i = findindex(L, t, key);  /* find original element */
  for (; i < t->sizearray; i++) {  /* try first array part */
    if (!ttisnil(&t->array[i])) {  /* a non-nil value? */
	  /* 加1是因为 lua array key 以1为起始 */
      setivalue(key, i + 1);
	  /* 存入 value */
      setobj2s(L, key+1, &t->array[i]);
      return 1;
    }
  }
  for (i -= t->sizearray; cast_int(i) < sizenode(t); i++) {  /* hash part */
    if (!ttisnil(gval(gnode(t, i)))) {  /* a non-nil value? */
      setobj2s(L, key, gkey(gnode(t, i)));
      setobj2s(L, key+1, gval(gnode(t, i)));
      return 1;
    }
  }
  return 0;  /* no more elements */
}


/*
** {=============================================================
** Rehash
** ==============================================================
*/

/*
** Compute the optimal size for the array part of table 't'. 'nums' is a
** "count array" where 'nums[i]' is the number of integers in the table
** between 2^(i - 1) + 1 and 2^i. Put in '*narray' the optimal size, and
** return the number of elements that will go to that part.
*/
/**
 * nums 数组中包含 table 中所有整数 key 元素个数 
 * *narray: nums 数组中所有元素之和
 */
static unsigned int computesizes (unsigned int nums[], unsigned int *narray) {
  int i;
  unsigned int twotoi;  /* 2^i */
  unsigned int a = 0;  /* number of elements smaller than 2^i */
  unsigned int na = 0;  /* number of elements to go to array part */
  unsigned int n = 0;  /* optimal size for array part */
  for (i = 0, twotoi = 1; twotoi/2 < *narray; i++, twotoi *= 2) {
    if (nums[i] > 0) {
      a += nums[i];
      if (a > twotoi/2) {  /* more than half elements present? */
        n = twotoi;  /* optimal size (till now) */
        na = a;  /* all elements up to 'n' will go to array part */
      }
    }
    if (a == *narray) break;  /* all elements already counted */
  }
  *narray = n;
  lua_assert(*narray/2 <= na && na <= *narray);
  return na;
}

/* 辅助函数, 用来将 node hash part 的整数 key 添加到对应 nums 数组 */
static int countint (const TValue *key, unsigned int *nums) {
  unsigned int k = arrayindex(key);
  if (k != 0) {  /* is 'key' an appropriate array index? */
    nums[luaO_ceillog2(k)]++;  /* count as such */
    return 1;
  }
  else
    return 0;
}


/*
 * 用来计算上面 computersizes 函数中的 nums 数组;
 * 返回 array 中非 nil 元素个数
 */
static unsigned int numusearray (const Table *t, unsigned int *nums) {
  int lg;
  unsigned int ttlg;  /* 2^lg */
  unsigned int ause = 0;  /* summation of 'nums' */
  unsigned int i = 1;  /* count to traverse all array keys */
  /* traverse each slice */
  for (lg = 0, ttlg = 1; lg <= MAXABITS; lg++, ttlg *= 2) {
    unsigned int lc = 0;  /* counter */
    unsigned int lim = ttlg;
    if (lim > t->sizearray) {
      lim = t->sizearray;  /* adjust upper limit */
      if (i > lim)
        break;  /* no more elements to count */
    }
    /* count elements in range (2^(lg - 1), 2^lg] */
    for (; i <= lim; i++) {
      if (!ttisnil(&t->array[i-1]))
        lc++;
    }
    nums[lg] += lc;
    ause += lc;
  }
  return ause;
}


/**
 * 将 node hash part 中的整数 key 添加到 nums 数组中;
 * 
 * *pnasize += ause; ause 为 添加到 nums 数组中的元素个数;
 *
 * 返回 node hash part 中元素个数;
 */
static int numusehash (const Table *t, unsigned int *nums,
                       unsigned int *pnasize) {
  int totaluse = 0;  /* total number of elements */
  int ause = 0;  /* elements added to 'nums' (can go to array part) */
  int i = sizenode(t);
  while (i--) {
    Node *n = &t->node[i];
    if (!ttisnil(gval(n))) {
      ause += countint(gkey(n), nums);
      totaluse++;
    }
  }
  *pnasize += ause;
  return totaluse;
}


/**
 * 重新分配 table 中 array 的大小为 size, 增大的话新元素置为 nil
 */
static void setarrayvector (lua_State *L, Table *t, unsigned int size) {
  unsigned int i;
  luaM_reallocvector(L, t->array, t->sizearray, size, TValue);
  for (i=t->sizearray; i<size; i++)
     setnilvalue(&t->array[i]);
  t->sizearray = size;
}


/**
 * 新建 table 中的 node hash 表, 大小为 2 ^ ceil( log2(size) ), 
 * size 为 0 表示空表.
 */
static void setnodevector (lua_State *L, Table *t, unsigned int size) {
  int lsize;
  if (size == 0) {  /* no elements to hash part? */
    t->node = cast(Node *, dummynode);  /* use common 'dummynode' */
    lsize = 0;
  }
  else {
    int i;
    lsize = luaO_ceillog2(size);
    if (lsize > MAXHBITS)
      luaG_runerror(L, "table overflow");
    size = twoto(lsize);
    t->node = luaM_newvector(L, size, Node);
    for (i = 0; i < (int)size; i++) {
	  /* 初始化 node hash 表中每个元素 */
      Node *n = gnode(t, i);
      gnext(n) = 0;
      setnilvalue(wgkey(n));
      setnilvalue(gval(n));
    }
  }
  t->lsizenode = cast_byte(lsize);
  t->lastfree = gnode(t, size);  /* all positions are free */
}


/**
 * table sequence 的大小改为 nasize，node hash 表的大小改为 nhsize;
 * 并会重新调整 array 和 node hash table 中的数据
 */
/* TODO array part grow 的时候，整数 key 会插入 array part 吗？*/
void luaH_resize (lua_State *L, Table *t, unsigned int nasize,
                                          unsigned int nhsize) {
  unsigned int i;
  int j;
  unsigned int oldasize = t->sizearray;
  int oldhsize = t->lsizenode;
  Node *nold = t->node;  /* save old hash ... */
  if (nasize > oldasize)  /* array part must grow? */
    setarrayvector(L, t, nasize);
  /* create new hash part with appropriate size */
  setnodevector(L, t, nhsize);
  if (nasize < oldasize) {  /* array part must shrink? */
    t->sizearray = nasize;
    /* re-insert elements from vanishing slice */
    for (i=nasize; i<oldasize; i++) {
      /* lua 起始位置为1，所以下面的 key 是 i + 1 */
      /* 都会插入到 node hash table 中 */
      if (!ttisnil(&t->array[i]))
        luaH_setint(L, t, i + 1, &t->array[i]);
    }
    /* shrink array */
    luaM_reallocvector(L, t->array, oldasize, nasize, TValue);
  }
  /* re-insert elements from hash part */
  for (j = twoto(oldhsize) - 1; j >= 0; j--) {
    Node *old = nold + j;
    if (!ttisnil(gval(old))) {
      /* doesn't need barrier/invalidate cache, as entry was
         already present in the table */
      setobjt2t(L, luaH_set(L, t, gkey(old)), gval(old));
    }
  }
  if (!isdummy(nold))
    luaM_freearray(L, nold, cast(size_t, twoto(oldhsize))); /* free old array */
}


/**
 * 重新分配 array 大小 
 */
void luaH_resizearray (lua_State *L, Table *t, unsigned int nasize) {
  int nsize = isdummy(t->node) ? 0 : sizenode(t);
  luaH_resize(L, t, nasize, nsize);
}

/*
** nums[i] = number of keys 'k' where 2^(i - 1) < k <= 2^i
*/
/**
 * 插入 key ek, 并重新 hash, 使得 array part 至少一半使用
 */
static void rehash (lua_State *L, Table *t, const TValue *ek) {
  unsigned int nasize, na;
  unsigned int nums[MAXABITS + 1];
  int i;
  int totaluse;
  for (i = 0; i <= MAXABITS; i++) nums[i] = 0;  /* reset counts */
  nasize = numusearray(t, nums);  /* count keys in array part */
  totaluse = nasize;  /* all those keys are integer keys */
  totaluse += numusehash(t, nums, &nasize);  /* count keys in hash part */
  /* count extra key */
  nasize += countint(ek, nums);
  totaluse++;
  /* 此时的nasize: table 中整数key的数目, 即 nums 数组元素之和 */

  /* compute new size for array part */
  na = computesizes(nums, &nasize);
  /* resize the table to new computed sizes */
  luaH_resize(L, t, nasize, totaluse - na);
}



/*
** }=============================================================
*/


/* 新建并初始化 Table */
Table *luaH_new (lua_State *L) {
  GCObject *o = luaC_newobj(L, LUA_TTABLE, sizeof(Table));
  Table *t = gco2t(o);
  t->metatable = NULL;
  t->flags = cast_byte(~0);
  t->array = NULL;
  t->sizearray = 0;
  setnodevector(L, t, 0);
  return t;
}


/**
 * 释放 table 内存, 包括其内的 array 和 node table
 */
void luaH_free (lua_State *L, Table *t) {
  if (!isdummy(t->node))
    luaM_freearray(L, t->node, cast(size_t, sizenode(t)));
  luaM_freearray(L, t->array, t->sizearray);
  luaM_free(L, t);
}


/**
 * 在 node table 中获取一个 free pos, 并且更新 t->lastfree;
 * 没有 free pos 就返回 NULL
 */
static Node *getfreepos (Table *t) {
  while (t->lastfree > t->node) {
    t->lastfree--;
    if (ttisnil(gkey(t->lastfree)))
      return t->lastfree;
  }
  return NULL;  /* could not find a free place */
}



/*
** inserts a new key into a hash table; first, check whether key's main
** position is free. If not, check whether colliding node is in its main
** position or not: if it is not, move colliding node to an empty place and
** put new key in its main position; otherwise (colliding node is in its main
** position), new key goes to an empty position.
*/
/**
 * 在hash table中插入一个key, 返回 key 所对应的值的地址, 核心就是对于冲突
 * 位置的处理，非常有意思的方法 
 */
TValue *luaH_newkey (lua_State *L, Table *t, const TValue *key) {
  Node *mp;
  TValue aux;
  if (ttisnil(key)) luaG_runerror(L, "table index is nil");
  else if (ttisfloat(key)) {
    lua_Number n = fltvalue(key);
    lua_Integer k;
    if (luai_numisnan(n))
      luaG_runerror(L, "table index is NaN");
    if (numisinteger(n, &k)) {  /* index is int? */
      setivalue(&aux, k);
      key = &aux;  /* insert it as an integer */
    }
  }
  mp = mainposition(t, key);
  if (!ttisnil(gval(mp)) || isdummy(mp)) {  /* main position is taken? */
    Node *othern;
    Node *f = getfreepos(t);  /* get a free place */
    if (f == NULL) {  /* cannot find a free place? */
      rehash(L, t, key);  /* grow table */
      /* whatever called 'newkey' takes care of TM cache and GC barrier */
      return luaH_set(L, t, key);  /* insert key into grown table */
    }
    lua_assert(!isdummy(f));
    othern = mainposition(t, gkey(mp));
    if (othern != mp) {  /* is colliding node out of its main position? */
      /* yes; move colliding node into free position */
      while (othern + gnext(othern) != mp)  /* find previous */
        othern += gnext(othern);
      gnext(othern) = cast_int(f - othern);  /* rechain to point to 'f' */
      *f = *mp;  /* copy colliding node into free pos. (mp->next also goes) */
      if (gnext(mp) != 0) {
        gnext(f) += cast_int(mp - f);  /* correct 'next' */
        gnext(mp) = 0;  /* now 'mp' is free */
      }
      setnilvalue(gval(mp));
    }
    else {  /* colliding node is in its own main position */
      /* new node will go into free position */
      if (gnext(mp) != 0)
        gnext(f) = cast_int((mp + gnext(mp)) - f);  /* chain new position */
      else lua_assert(gnext(f) == 0);
      gnext(mp) = cast_int(f - mp);
      mp = f;
    }
  }
  setnodekey(L, &mp->i_key, key);
  luaC_barrierback(L, t, key);
  lua_assert(ttisnil(gval(mp)));
  return gval(mp);
}


/*
** search function for integers
*/
/**
 * 在Table中查找属性 key 的值, key 是一个整数类型，存在返回其地址，否则返回 nil;
 * lua中 table 模拟的数组下标是1开始的, 若key范围在table array内，则会返回
 * array对应位置的值，否则会在node数组中查找
 */
const TValue *luaH_getint (Table *t, lua_Integer key) {
  /* (1 <= key && key <= t->sizearray) */
  if (l_castS2U(key - 1) < t->sizearray)
    return &t->array[key - 1];
  else {
    /* 超过了数组大小, 则在node数组中查找 */
    Node *n = hashint(t, key);
    for (;;) {  /* check whether 'key' is somewhere in the chain */
      if (ttisinteger(gkey(n)) && ivalue(gkey(n)) == key)
        return gval(n);  /* that's it */
      else {
        int nx = gnext(n);
        if (nx == 0) break;
        n += nx;
      }
    };
    return luaO_nilobject;
  }
}


/*
** search function for short strings
*/
/**
 * &t[key], key 是一个短字符串，存在返回其值，否则返回 nil
 */
const TValue *luaH_getstr (Table *t, TString *key) {
  /* 获得 key 字符串在表 node 数组中的位置 */
  Node *n = hashstr(t, key);
  lua_assert(key->tt == LUA_TSHRSTR);
  for (;;) {  /* check whether 'key' is somewhere in the chain */
    const TValue *k = gkey(n);
    if (ttisshrstring(k) && eqshrstr(tsvalue(k), key))
      return gval(n);  /* that's it */
    else {
      int nx = gnext(n);
      if (nx == 0) break;
      n += nx;
    }
  };
  return luaO_nilobject;
}


/*
** main search function
*/
/**
 * 返回 &t[key], 不触发元方法
 */
const TValue *luaH_get (Table *t, const TValue *key) {
  switch (ttype(key)) {
    case LUA_TSHRSTR: return luaH_getstr(t, tsvalue(key));
    case LUA_TNUMINT: return luaH_getint(t, ivalue(key));
    case LUA_TNIL: return luaO_nilobject;
    case LUA_TNUMFLT: {
      lua_Integer k;
      if (numisinteger(fltvalue(key), &k)) /* index is int? */
        return luaH_getint(t, k);  /* use specialized version */
      /* else go through */
    }
    default: {
      Node *n = mainposition(t, key);
      for (;;) {  /* check whether 'key' is somewhere in the chain */
        if (luaV_rawequalobj(gkey(n), key))
          return gval(n);  /* that's it */
        else {
          int nx = gnext(n);
          if (nx == 0) break;
          n += nx;
        }
      };
      return luaO_nilobject;
    }
  }
}


/*
** beware: when using this function you probably need to check a GC
** barrier and invalidate the TM cache.
*/
/**
 * 若表中没有key, 则会新建这个key, 返回 key 对应的值的地址 
 */
TValue *luaH_set (lua_State *L, Table *t, const TValue *key) {
  const TValue *p = luaH_get(t, key);
  if (p != luaO_nilobject)
    return cast(TValue *, p);
  else return luaH_newkey(L, t, key);
}


/**
 * 向表中插入 key-value 对, key 存在就用 value 覆盖原来的值, 否则就是新建对
 */
void luaH_setint (lua_State *L, Table *t, lua_Integer key, TValue *value) {
  const TValue *p = luaH_getint(t, key);
  TValue *cell;
  if (p != luaO_nilobject)
    cell = cast(TValue *, p);
  else {
    /* key 超过了 array 大小, 插入到 node hash table 中 */
    TValue k;
    setivalue(&k, key);
    cell = luaH_newkey(L, t, &k);
  }
  setobj2t(L, cell, value);
}


/**
 * 在hash table 中查找最小的这样一个整数i >= j, 
 * 使 t[i] != nil && t[i + 1] == nil
 * 
 * 对查找过程进行了优化, 不是单纯的线性查找，最好情况下可以用二分查找, 
 * 但最坏情况也有可能退化为线性查找
 */
static int unbound_search (Table *t, unsigned int j) {
  unsigned int i = j;  /* i is zero or a present index */
  j++;
  /* find 'i' and 'j' such that i is present and j is not */
  while (!ttisnil(luaH_getint(t, j))) {
    i = j;
    if (j > cast(unsigned int, MAX_INT)/2) {  /* overflow? */
      /* table was built with bad purposes: resort to linear search */
      i = 1;
      while (!ttisnil(luaH_getint(t, i))) i++;
	  /* t[i] is nil, t[i - 1] is not nil */
      return i - 1;
    }
    j *= 2;
  }
  /* t[j] is nil */
  /* now do a binary search between them */
  while (j - i > 1) {
    unsigned int m = (i+j)/2;
    if (ttisnil(luaH_getint(t, m))) j = m;
    else i = m;
  }
  return i;
}


/*
** Try to find a boundary in table 't'. A 'boundary' is an integer index
** such that t[i] is non-nil and t[i+1] is nil (and 0 if t[1] is nil).
*/
/* 先在 array 中找，再在 hash part 中找 */ 
int luaH_getn (Table *t) {
  unsigned int j = t->sizearray;
  if (j > 0 && ttisnil(&t->array[j - 1])) {
    /* there is a boundary in the array part: (binary) search for it */
    unsigned int i = 0;
    while (j - i > 1) {
      unsigned int m = (i+j)/2;
      if (ttisnil(&t->array[m - 1])) j = m;
      else i = m;
    }
    return i;
  }
  /* else must find a boundary in hash part */
  else if (isdummy(t->node))  /* hash part is empty? */
    return j;  /* that is easy... */
  else return unbound_search(t, j);
}



#if defined(LUA_DEBUG)

Node *luaH_mainposition (const Table *t, const TValue *key) {
  return mainposition(t, key);
}

int luaH_isdummy (Node *n) { return isdummy(n); }

#endif
