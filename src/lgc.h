/*
** $Id: lgc.h,v 2.86 2014/10/25 11:50:46 roberto Exp $
** Garbage Collector
** See Copyright Notice in lua.h
*/

/** 
 * 参考:
 *   http://lua-users.org/wiki/EmergencyGarbageCollector 
 *   http://www.memorymanagement.org/mmref/recycle.html
 *   http://www.iecc.com/gclist/GC-algorithms.html
 *   http://www.lua.org/manual/5.3/manual.html#2.5
 */

/**
 * 3.1.3. Incremental collection
 * Older garbage collection algorithms relied on being able to start 
 * collection and continue working until the collection was complete, 
 * without interruption. This makes many interactive systems pause 
 * during collection, and makes the presence of garbage collection 
 * obtrusive.
 * 
 * Fortunately, there are modern techniques (known as incremental 
 * garbage collection) to allow garbage collection to be performed 
 * in a series of small steps while the program is never stopped 
 * for long. In this context, the program that uses and modifies 
 * the blocks is sometimes known as the mutator. While the collector 
 * is trying to determine which blocks of memory are reachable by
 * the mutator, the mutator is busily allocating new blocks, modifying
 * old blocks, and changing the set of blocks it is actually looking at.
 * Incremental collection is usually achieved with either the cooperation 
 * of the memory hardware or the mutator; this ensures that, whenever
 * memory in crucial locations is accessed, a small amount of necessary
 * bookkeeping is performed to keep the collector’s data structures correct.
 */

#ifndef lgc_h
#define lgc_h


#include "lobject.h"
#include "lstate.h"

/*
** Collectable objects may have one of three colors: white, which
** means the object is not marked; gray, which means the
** object is marked, but its references may be not marked; and
** black, which means that the object and all its references are marked.
** The main invariant of the garbage collector, while marking objects,
** is that a black object can never point to a white one. Moreover,
** any gray object must be in a "gray list" (gray, grayagain, weak,
** allweak, ephemeron) so that it can be visited again before finishing
** the collection cycle. These lists have no meaning when the invariant
** is not being enforced (e.g., sweep phase).
*/


/* how much to allocate before next GC step */
#if !defined(GCSTEPSIZE)
/* ~100 small strings */
#define GCSTEPSIZE	(cast_int(100 * sizeof(TString)))
#endif


/**
 * Lua 5.1 version
 * 
 * Garbage collector states (each collection cycle passes through 
 * these states in this order):
 *
 * GCSpause - Start of collection cycle. At this state all objects 
 * should be marked with the current white. The main lua_State, globals 
 * table, registry, and metatables are marked gray and added to the gray 
 * list. The state now changes to GCSpropagate.
 * 
 * GCSpropagate - Each object in the gray list is removed and marked black,
 * then any white (type 0 or 1) objects it references are marked gray and
 * added to the gray list. Once the gray list is empty the current white is
 * switched to the other white. All objects marked with the old white type
 * are now dead objects. The state now changes to GCSsweepstring.
 * 
 * GCSsweepstring - The color of each string in the internal strings 
 * hashtable is checked. If the color matches the old white type that
 * string is dead and is freed. If the color matches the current white 
 * (newly created string) or is gray (some other object references it), 
 * then it is alive and its color is reset to the current white. Once all 
 * strings are checked the state is changed to GCSsweep.
 *
 * GCSsweep - The color of each objects in the global rootgc list 
 * (this list holds all objects except strings) is checked just like the 
 * strings during the GCSsweepstring state. Dead objects are freed and 
 * removed from the rootgc list. Live objects have their color reset to the 
 * current white. Once all objects have been checked the state is changed 
 * to GCSfinalize.
 * 
 * GCSfinalize - This state will finalize all dead userdata objects by 
 * running their "__gc" metamethod. Once all dead userdata objects have 
 * been finailzed the state is changed to GCSpause and this completes a 
 * cycle of the garbage collector.
 */

/*
** Possible states of the Garbage Collector
*/
#define GCSpropagate	0

/* 详见 lgc.c 的 atomic 函数 */
#define GCSatomic	1

/* sweep phase begin */

/* sweep "regular" objects */
#define GCSswpallgc	2
/* sweep g->finobj */
/* sweep objects with finalizers */
#define GCSswpfinobj	3
/* sweep g->tobefnz */
/* sweep objects to be finalized */
#define GCSswptobefnz	4
/* sweep main thread */
#define GCSswpend	5
/* sweep phase end */

/* call remaining finalizers */
#define GCScallfin	6
#define GCSpause	7


#define issweepphase(g)  \
	(GCSswpallgc <= (g)->gcstate && (g)->gcstate <= GCSswpend)


/*
** macro to tell when main invariant (white objects cannot point to black
** ones) must be kept. During a collection, the sweep
** phase may break the invariant, as objects turned white may point to
** still-black objects. The invariant is restored when sweep ends and
** all objects are white again.
*/

#define keepinvariant(g)	((g)->gcstate <= GCSatomic)


/*
** some useful bit tricks
*/
#define resetbits(x,m)		((x) &= cast(lu_byte, ~(m)))
#define setbits(x,m)		((x) |= (m))
#define testbits(x,m)		((x) & (m))
#define bitmask(b)		(1<<(b))
#define bit2mask(b1,b2)		(bitmask(b1) | bitmask(b2))
#define l_setbit(x,b)		setbits(x, bitmask(b))
#define resetbit(x,b)		resetbits(x, bitmask(b))
#define testbit(x,b)		testbits(x, bitmask(b))


/* Layout for bit use in 'marked' field: */
#define WHITE0BIT	0  /* object is white (type 0) */
#define WHITE1BIT	1  /* object is white (type 1) */
#define BLACKBIT	2  /* object is black */
#define FINALIZEDBIT	3  /* object has been marked for finalization */
/* bit 7 is currently used by tests (luaL_checkmemory) */

#define WHITEBITS	bit2mask(WHITE0BIT, WHITE1BIT)


/**
 * An object's color is defined by which of the first 3 bits 
 * (0, 1, 2) are set:
 * 
 * It is white if one of the two white bits (0,1) are set and the black 
 * bit is clear. Only one white bit should be used by a white object.
 * 
 * It is gray if all three color bits (0,1,2) are clear.
 * 
 * It is black if the black bit is set and the two white bits are clear.
 */
#define iswhite(x)      testbits((x)->marked, WHITEBITS)
#define isblack(x)      testbit((x)->marked, BLACKBIT)
#define isgray(x)  /* neither white nor black */  \
	(!testbits((x)->marked, WHITEBITS | bitmask(BLACKBIT)))

#define tofinalize(x)	testbit((x)->marked, FINALIZEDBIT)

#define otherwhite(g)	((g)->currentwhite ^ WHITEBITS)
/**
 * ow: otherwhite, m: marked 
 * m 的 whitebit 与 ow 相同返回 true, 否则返回 false
 */
#define isdeadm(ow,m)	(!(((m) ^ WHITEBITS) & (ow)))
/**  
 * The garbage collector keeps track of a current white (type 0 or 1) 
 * and objects with the other white are dead objects that can be collected
 * during the sweep states.
 */
/* dead: 资源v已经无引用， 但还没有被垃圾收集器回收, 在 sweep 环节回收 */
#define isdead(g,v)	isdeadm(otherwhite(g), (v)->marked)

/* TODO 注意这里用的是异或 */
#define changewhite(x)	((x)->marked ^= WHITEBITS)
/* 将对象设为黑色 */
#define gray2black(x)	l_setbit((x)->marked, BLACKBIT)

/**
 * 新创建对象时设置对象的 mark 为 currentwhite
 */
#define luaC_white(g)	cast(lu_byte, (g)->currentwhite & WHITEBITS)


#define luaC_condGC(L,c) \
	{if (G(L)->GCdebt > 0) {c;}; condchangemem(L);}
#define luaC_checkGC(L)		luaC_condGC(L, luaC_step(L);)


#define luaC_barrier(L,p,v) {  \
	if (iscollectable(v) && isblack(p) && iswhite(gcvalue(v)))  \
	luaC_barrier_(L,obj2gco(p),gcvalue(v)); }

#define luaC_barrierback(L,p,v) {  \
	if (iscollectable(v) && isblack(p) && iswhite(gcvalue(v)))  \
	luaC_barrierback_(L,p); }

#define luaC_objbarrier(L,p,o) {  \
	if (isblack(p) && iswhite(o)) \
		luaC_barrier_(L,obj2gco(p),obj2gco(o)); }

#define luaC_upvalbarrier(L,uv) \
  { if (iscollectable((uv)->v) && !upisopen(uv)) \
         luaC_upvalbarrier_(L,uv); }

/* 标记永远不回收 o 对象 */
LUAI_FUNC void luaC_fix (lua_State *L, GCObject *o);
LUAI_FUNC void luaC_freeallobjects (lua_State *L);
LUAI_FUNC void luaC_step (lua_State *L);
LUAI_FUNC void luaC_runtilstate (lua_State *L, int statesmask);
LUAI_FUNC void luaC_fullgc (lua_State *L, int isemergency);
/**
 * tt: tag, 对象类型
 * sz: 对象大小
 */
LUAI_FUNC GCObject *luaC_newobj (lua_State *L, int tt, size_t sz);
LUAI_FUNC void luaC_barrier_ (lua_State *L, GCObject *o, GCObject *v);
LUAI_FUNC void luaC_barrierback_ (lua_State *L, Table *o);
LUAI_FUNC void luaC_upvalbarrier_ (lua_State *L, UpVal *uv);
LUAI_FUNC void luaC_checkfinalizer (lua_State *L, GCObject *o, Table *mt);
/**
 * uv 的引用次数减一，为 0 时则将其回收
 */
LUAI_FUNC void luaC_upvdeccount (lua_State *L, UpVal *uv);


#endif
