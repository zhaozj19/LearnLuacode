/*
** $Id: lstate.c,v 2.133 2015/11/13 12:16:51 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#define lstate_c
#define LUA_CORE

#include "lprefix.h"


#include <stddef.h>
#include <string.h>

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"



// LUAI_GCPAUSE是g->gcpause的初始值，用于计算触发GC操作的阈值
#if !defined(LUAI_GCPAUSE)
#define LUAI_GCPAUSE	200  /* 200% */
#endif

// LUAI_GCMUL用于控制GC运行速度相对于内存分配的倍数
#if !defined(LUAI_GCMUL)
#define LUAI_GCMUL	200 /* GC runs 'twice the speed' of memory allocation */
#endif


/*
** a macro to help the creation of a unique random seed when a state is
** created; the seed is used to randomize hashes.
*/
// 当一个线程被创建的时候，luai_makeseed用来创建一个独一无二的随机种子，执行字符串哈希操作时用到了这个种子
// time(null)获取当前日期的秒数,返回类型为time_t，具体类型是平台依赖的，在这里强转成unsigned int(0~4294967295),0~2^32-1,此处默认int为32位
#if !defined(luai_makeseed)
#include <time.h>
#define luai_makeseed()		cast(unsigned int, time(NULL))
#endif



/*
** thread state + extra space
*/
// 线程空间包含一个lua_State和一个额外数据空间
// 这里的额外空间用于存放一个指针，参考资料说："程序可以把这块内存用于任何用途；而lua不会使用它"。 不懂什么意思，先记这里
typedef struct LX {
  lu_byte extra_[LUA_EXTRASPACE];
  lua_State l;
} LX;


/*
** Main thread combines a thread state and the global state
*/
// LG结构包含一个global_State和一个lua_State(外加extra_[LUA_EXTRASPACE])，其目的是为了分配内存时，让global_State和lua_State的创建和初始化一起完成
typedef struct LG {
  LX l;
  global_State g;
} LG;



#define fromstate(L)	(cast(LX *, cast(lu_byte *, (L)) - offsetof(LX, l)))


/*
** Compute an initial seed as random as possible. Rely on Address Space
** Layout Randomization (if present) to increase randomness..
*/
#define addbuff(b,p,e) \
  { size_t t = cast(size_t, e); \
    memcpy(b + p, &t, sizeof(t)); p += sizeof(t); }

static unsigned int makeseed (lua_State *L) {
  char buff[4 * sizeof(size_t)];
  unsigned int h = luai_makeseed();
  int p = 0;
  addbuff(buff, p, L);  /* heap variable */
  addbuff(buff, p, &h);  /* local variable */
  addbuff(buff, p, luaO_nilobject);  /* global variable */
  addbuff(buff, p, &lua_newstate);  /* public function */
  lua_assert(p == sizeof(buff));
  return luaS_hash(buff, p, h);
}


/*
** set GCdebt to a new value keeping the value (totalbytes + GCdebt)
** invariant (and avoiding underflows in 'totalbytes')
*/
void luaE_setdebt (global_State *g, l_mem debt) {
  l_mem tb = gettotalbytes(g);
  lua_assert(tb > 0);
  if (debt < tb - MAX_LMEM)
    debt = tb - MAX_LMEM;  /* will make 'totalbytes == MAX_LMEM' */
  g->totalbytes = tb - debt;
  g->GCdebt = debt;
}


CallInfo *luaE_extendCI (lua_State *L) {
  CallInfo *ci = luaM_new(L, CallInfo);
  lua_assert(L->ci->next == NULL);
  L->ci->next = ci;
  ci->previous = L->ci;
  ci->next = NULL;
  L->nci++;
  return ci;
}


/*
** free all CallInfo structures not in use by a thread
*/
void luaE_freeCI (lua_State *L) {
  CallInfo *ci = L->ci;
  CallInfo *next = ci->next;
  ci->next = NULL;
  while ((ci = next) != NULL) {
    next = ci->next;
    luaM_free(L, ci);
    L->nci--;
  }
}


/*
** free half of the CallInfo structures not in use by a thread
*/
void luaE_shrinkCI (lua_State *L) {
  CallInfo *ci = L->ci;
  CallInfo *next2;  /* next's next */
  /* while there are two nexts */
  while (ci->next != NULL && (next2 = ci->next->next) != NULL) {
    luaM_free(L, ci->next);  /* free next */
    L->nci--;
    ci->next = next2;  /* remove 'next' from the list */
    next2->previous = ci;
    ci = next2;  /* keep next's next */
  }
}

// 初始化线程
// luaM_newvector创建40个TValue类型的内存空间，起始地址赋给stack
// 数据栈尺寸的话就是BASIC_STACK_SIZE，并且把这40各元素全置为nil
// 数据栈的栈顶就是栈底stack，数据栈的上限为L1->stack + L1->stacksize - EXTRA_STACK;此处空一个额外的地址空间备用
// 接着初始化调用函数的信息列表
static void stack_init (lua_State *L1, lua_State *L) {
  int i; CallInfo *ci;
  /* initialize stack array */
  L1->stack = luaM_newvector(L, BASIC_STACK_SIZE, TValue);
  L1->stacksize = BASIC_STACK_SIZE;
  for (i = 0; i < BASIC_STACK_SIZE; i++)
    setnilvalue(L1->stack + i);  /* erase new stack */
  L1->top = L1->stack;
  L1->stack_last = L1->stack + L1->stacksize - EXTRA_STACK;
  /* initialize first ci */
  ci = &L1->base_ci;
  ci->next = ci->previous = NULL;
  ci->callstatus = 0;
  ci->func = L1->top;
  setnilvalue(L1->top++);  /* 'function' entry for this 'ci' */
  ci->top = L1->top + LUA_MINSTACK;
  L1->ci = ci;
}


static void freestack (lua_State *L) {
  if (L->stack == NULL)
    return;  /* stack not completely built yet */
  L->ci = &L->base_ci;  /* free the entire 'ci' list */
  luaE_freeCI(L);
  lua_assert(L->nci == 0);
  luaM_freearray(L, L->stack, L->stacksize);  /* free stack array */
}


/*
** Create registry table and its predefined values
*/
static void init_registry (lua_State *L, global_State *g) {
  TValue temp;
  /* create registry */
  Table *registry = luaH_new(L);
  sethvalue(L, &g->l_registry, registry);
  luaH_resize(L, registry, LUA_RIDX_LAST, 0);
  /* registry[LUA_RIDX_MAINTHREAD] = L */
  setthvalue(L, &temp, L);  /* temp = L */
  luaH_setint(L, registry, LUA_RIDX_MAINTHREAD, &temp);
  /* registry[LUA_RIDX_GLOBALS] = table of globals */
  sethvalue(L, &temp, luaH_new(L));  /* temp = new table (global table) */
  luaH_setint(L, registry, LUA_RIDX_GLOBALS, &temp);
}


/*
** open parts of the state that may cause memory-allocation errors.
** ('g->version' != NULL flags that the state was completely build)
*/
static void f_luaopen (lua_State *L, void *ud) {
  global_State *g = G(L);
  UNUSED(ud);
  stack_init(L, L);  /* init stack */
  init_registry(L, g);
  luaS_init(L);
  luaT_init(L);
  luaX_init(L);
  g->gcrunning = 1;  /* allow gc */
  g->version = lua_version(NULL);
  luai_userstateopen(L);
}


/*
** preinitialize a thread with consistent values without allocating
** any memory (to avoid errors)
*/
// 用一些没有分配任何内存的缺省值(避免错误)预初始化一个线程

static void preinit_thread (lua_State *L, global_State *g) {
  G(L) = g;               /*设置L的全局状态机g*/
  L->stack = NULL;        /*设置L的数据栈的栈底*/
  L->ci = NULL;           /*设置L的当前运行函数的调用信息*/
  L->nci = 0;             /*设置L的'ci'列表的项数*/
  L->stacksize = 0;       /*设置L的数据栈的大小*/
  L->twups = L;  /* thread has no upvalues */
  L->errorJmp = NULL;     /*设置L的错误恢复点*/
  L->nCcalls = 0;         /*设置L的CallStack动态增减过程中调用C函数的个数*/
  L->hook = NULL;         /*hook是用来调试钩子的，具体细节我不清楚*/
  L->hookmask = 0;        /*hookmask我也不清楚，后面再补充*/
  L->basehookcount = 0;
  L->allowhook = 1;
  resethookcount(L);
  L->openupval = NULL;    /*设置L的open upvalues列表*/
  L->nny = 1;
  L->status = LUA_OK;     /*设置L的初始状态*/
  L->errfunc = 0;         /*设置L的当亲错误的函数句柄 (stack index)*/
}


static void close_state (lua_State *L) {
  global_State *g = G(L);
  luaF_close(L, L->stack);  /* close all upvalues for this thread */
  luaC_freeallobjects(L);  /* collect all objects */
  if (g->version)  /* closing a fully built state? */
    luai_userstateclose(L);
  luaM_freearray(L, G(L)->strt.hash, G(L)->strt.size);
  freestack(L);
  lua_assert(gettotalbytes(g) == sizeof(LG));
  (*g->frealloc)(g->ud, fromstate(L), sizeof(LG), 0);  /* free main block */
}


LUA_API lua_State *lua_newthread (lua_State *L) {
  global_State *g = G(L);
  lua_State *L1;
  lua_lock(L);
  luaC_checkGC(L);
  /* create new thread */
  L1 = &cast(LX *, luaM_newobject(L, LUA_TTHREAD, sizeof(LX)))->l;
  L1->marked = luaC_white(g);
  L1->tt = LUA_TTHREAD;
  /* link it on list 'allgc' */
  L1->next = g->allgc;
  g->allgc = obj2gco(L1);
  /* anchor it on L stack */
  setthvalue(L, L->top, L1);
  api_incr_top(L);
  preinit_thread(L1, g);
  L1->hookmask = L->hookmask;
  L1->basehookcount = L->basehookcount;
  L1->hook = L->hook;
  resethookcount(L1);
  /* initialize L1 extra space */
  memcpy(lua_getextraspace(L1), lua_getextraspace(g->mainthread),
         LUA_EXTRASPACE);
  luai_userstatethread(L, L1);
  stack_init(L1, L);  /* init stack */
  lua_unlock(L);
  return L1;
}


void luaE_freethread (lua_State *L, lua_State *L1) {
  LX *l = fromstate(L1);
  luaF_close(L1, L1->stack);  /* close all upvalues for this thread */
  lua_assert(L1->openupval == NULL);
  luai_userstatefree(L, L1);
  freestack(L1);
  luaM_free(L, l);
}

// lua_newstate为我们创建并初始化一个lua虚拟机
// f指定虚拟机中的内存分配策略，第一块申请下来的内存用来存储global_State(全局状态机)和lua_State(主线程)
// 为了避免内存碎片的产生，同时减少内存分配和释放的次数，lua采用了一个小技巧：利用一个LG结构，把分配lua_State和global_State的行为联结在一起
// 然后得出来一个LG的内存区域，包含了global_State和lua_State，并用L和g指针指向它们，并将当前g的当前白色设置为WHITE0BIT(要用bitmask处理一下)
// 接下来，设置L的属性
// L->marked = luaC_white(g) 设置L的GC颜色
// preinit_thread(L, g)  预初始化L
// g->frealloc = f  设置全局状态机的内存分配策略
// g->ud = ud  ud是frealloc的参数
// g->mainthread = L  设置g的主线程
// g->seed = makeseed(L) 设置g的随机种子
// g->gcrunning = 0  因为刚创建state时，没有GC，所以设为0
// g->GCestimate = 0 使用的内存的估计值
// g->strt.size = g->strt.nuse = 0 全局字符串表的大小和当前数量
// g->strt.hash = NULL  字符串数组设为NULL
// setnilvalue(&g->l_registry) 全局的注册表设为nil
// g->panic = NULL 无保护错误发生时的处理函数
// g->version = NULL 版本号
// g->gckind = KGC_NORMAL  正在运行的GC种类
// g->allgc = g->finobj = g->tobefnz = g->fixedgc = NULL 和GC相关的属性，参考lstate.h
// 下面的设置就参考lstate.h吧
LUA_API lua_State *lua_newstate (lua_Alloc f, void *ud) {
  int i;
  lua_State *L;
  global_State *g;
  LG *l = cast(LG *, (*f)(ud, NULL, LUA_TTHREAD, sizeof(LG)));
  if (l == NULL) return NULL;
  L = &l->l.l;
  g = &l->g;
  L->next = NULL;
  L->tt = LUA_TTHREAD;
  g->currentwhite = bitmask(WHITE0BIT);
  L->marked = luaC_white(g);
  preinit_thread(L, g);
  g->frealloc = f;
  g->ud = ud;
  g->mainthread = L;
  g->seed = makeseed(L);
  g->gcrunning = 0;  /* no GC while building state */
  g->GCestimate = 0;
  g->strt.size = g->strt.nuse = 0;
  g->strt.hash = NULL;
  setnilvalue(&g->l_registry);
  g->panic = NULL;
  g->version = NULL;
  g->gcstate = GCSpause;
  g->gckind = KGC_NORMAL;
  g->allgc = g->finobj = g->tobefnz = g->fixedgc = NULL;
  g->sweepgc = NULL;
  g->gray = g->grayagain = NULL;
  g->weak = g->ephemeron = g->allweak = NULL;
  g->twups = NULL;
  g->totalbytes = sizeof(LG);
  g->GCdebt = 0;
  g->gcfinnum = 0;
  g->gcpause = LUAI_GCPAUSE;
  g->gcstepmul = LUAI_GCMUL;
  for (i=0; i < LUA_NUMTAGS; i++) g->mt[i] = NULL;  /*设置9种数据类型的元表*/
  if (luaD_rawrunprotected(L, f_luaopen, NULL) != LUA_OK) {
    /* memory allocation error: free partial state */
    close_state(L);
    L = NULL;
  }
  return L;
}

// 关闭主线程
LUA_API void lua_close (lua_State *L) {
  L = G(L)->mainthread;  /* only the main thread can be closed */
  lua_lock(L);
  close_state(L);
}


