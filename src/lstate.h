/*
** $Id: lstate.h,v 2.133 2016/12/22 13:08:50 roberto Exp $
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

/*
  关于垃圾回收对象的一些笔记：lua中的所有对象都要在释放前保持它的可访问性，因此
  所有对象都必须属于这些列表中的一个，这些列表就是用next连起来的'CommonHeader'链表中的一项
  'allgc':所有未被标记的对象
  'finobj':所有被标记的对象
  'tobefnz':所有准备被回收的的对象
  'fixedgc':所有不需要被回收的对象(只包含短字符串，例如保留字)
*/


// lua_longjmp结构体的里面包含
// jmp_buf类型的b和volatile int 类型的status，用来提供远程调用的功能(类似于跨函数的GOTO语句，GOTO语句只能在函数内部进行跳转)
struct lua_longjmp;  /* defined in ldo.c */


/*
** Atomic type (relative to signals) to better ensure that 'lua_sethook'
** is thread safe
*/
// 为了确保'lua_sethook'是线程安全的，这里最好用自动类型(相对于信号)    看不懂啥意思
// 当把变量声明为sig_atomic_t类型会保证该变量在使用或赋值时， 无论是在32位还是64位的机器上都能保证操作是原子的
// sig_atomic_t类型在linux中定义为int
#if !defined(l_signalT)
#include <signal.h>
#define l_signalT	sig_atomic_t
#endif


/* extra stack space to handle TM calls and some other extras */
// 额外的栈空间，用来处理TM调用和一些其他操作(TM调用是什么玩意)
#define EXTRA_STACK   5

// LUA_MINSTACK是定义在lua.h中的让C函数用的栈空间，默认20，这个地方乘以2，
// 也就是说，lua_state是有数据栈和调用栈的，而数据栈的初始大小就是BASIC_STACK_SIZE，当然不够用的时候还是可以扩展的
#define BASIC_STACK_SIZE        (2*LUA_MINSTACK)


/* kinds of Garbage Collection */
// 下面两个是垃圾回收类型

#define KGC_NORMAL	0
#define KGC_EMERGENCY	1	/* gc was forced by an allocation failure */

// 所有字符串均被存放在全局表（global_State）的 strt 域中。strt 是 string table 的简写，它是一个
// 哈希表
typedef struct stringtable {
  TString **hash;
  int nuse;  /* number of elements */
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

/*
  关于一个调用的信息汇总
  当一个线程被挂起的时候，'func'被调整为模拟顶部函数仅在堆栈中产生的值；
  以防万一，真实的'func'值被保存在extra字段
  当一个函数在一个协程里面调用另一个函数的时候，'extra'依然保持着函数的索引，在出错的情况下，这个协程函数能够在正确的栈顶被调用
  (完全不知道什么意思，翻译的很生硬)
*/


// CallInfo 的作用是维护一个函数调用的相关信息
// 在lua初始化的时候，分配了一个CallInfo数组，并用L->base_ci指向该数组第一个指针，用L->end_ci指向该数组最后一个指针，用L->size_ci记录当前数组的大小，L->ci记录的是当前被调用的闭包的调用信息

// StkId func :保存要调用的函数在数据栈上的位置
              // 需要记录这个信息，是因为如果当前是一个 lua 函数，且传入的参数个数不定的时候，需要用这个位置和当前数据栈底的位置相减，获得不定参数的准确数量。
// StkId top：闭包的栈使用限制，就是lua_push*的时候看着点，push太多就超了，可以用lua_checkstack来扩充
// struct CallInfo *previous, *next：因为CallInfo是一个闭包的调用信息，存在调用中的调用的情况，所以这里保存一下上下文信息
// l：供lua闭包使用的结构体
    // base：闭包调用的基址
    // savedpc：如果在本闭包中调用别的闭包，那么该值就保存下一条指令以便再返回的时候继续执行
// c：供C闭包使用的结构体
    // k：防止被挂起(k是什么，我还不咋清楚)
    // old_errfunc：函数错误时的栈索引
    // ctx：上下文信息，防止被挂起
// ptrdiff_t extra：（不知道）
// short nresults： 闭包要返回的值个数
// unsigned short callstatus：此处应该是一个调用状态
typedef struct CallInfo {
  StkId func;  /* function index in the stack */
  StkId	top;  /* top for this function */
  struct CallInfo *previous, *next;  /* dynamic call link */
  union {
    struct {  /* only for Lua functions */
      StkId base;  /* base for this function */
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
  unsigned short callstatus;
} CallInfo;


/*
** Bits in CallInfo status
*/
// CallInfo的一些状态位
// CIST_OAH：1 钩子最初的值
// CIST_LUA：2 正在被调用的是一个Lua函数
// CIST_HOOKED：4 正在被调用的是一个调试钩子
// CIST_FRESH：8 正在被调用的是luaV_execute的执行
// CIST_YPCALL：16 被调用的是一个被挂起的保护函数
// CIST_TAIL：32 被调用的是一个尾调用
// CIST_HOOKYIELD：64 上一个调用钩子被挂起了
// CIST_LEQ：128 using __lt for __le(看不懂)
// CIST_FIN:256 正在被调用的是个析构器(哈哈哈哈)
#define CIST_OAH	(1<<0)	/* original value of 'allowhook' */
#define CIST_LUA	(1<<1)	/* call is running a Lua function */
#define CIST_HOOKED	(1<<2)	/* call is running a debug hook */
#define CIST_FRESH	(1<<3)	/* call is running on a fresh invocation
                                   of luaV_execute */
#define CIST_YPCALL	(1<<4)	/* call is a yieldable protected call */
#define CIST_TAIL	(1<<5)	/* call was tail called */
#define CIST_HOOKYIELD	(1<<6)	/* last hook called yielded */
#define CIST_LEQ	(1<<7)  /* using __lt for __le */
#define CIST_FIN	(1<<8)  /* call is running a finalizer */

// 判断是否是lua闭包调用
#define isLua(ci)	((ci)->callstatus & CIST_LUA)

/* assume that CIST_OAH has offset 0 and that 'v' is strictly 0/1 */
// 确保CIST_OAH的初值为0，v一定要是0或1
#define setoah(st,v)	((st) = ((st) & ~CIST_OAH) | (v))
#define getoah(st)	((st) & CIST_OAH)


/*
** 'global state', shared by all threads of this state
*/
// global_State是lua全局状态机
// 一个lua虚拟机中只有一个全局的global_State，在调用lua_newstate时候，会创建和初始化这个全局结构，这个lua全局结构管理这lua中全局唯一的信息
// frealloc：虚拟机内存分配策略，可以在调用lua_newstate时指定参数，修改该策略，或者调用luaL_newstate函数使用默认的内存分配策略。也可以通过函数lua_setallocf：来设置内存分配策略
// *ud：是frealloc的参数
// totalbytes：当前分配的字节数(lu_mem和l_mem的定义在llimits.h中，分别是unsigned/signed整形)
// GCdebt：正在被垃圾收集器处理的字节，但是还没有被垃圾收集器还回来
// GCmemtrav：被GC过得内存数
// GCestimate：还在使用中的非垃圾内存的估计值
// strt：全局的字符串哈希表在，也就是那些短字符串，使得整个lua虚拟机中只有一份短字符串的实例
// l_registry：这是一个全局的注册表，其实就是一个全局的table(整个虚拟机中只有一个注册表),它只能被C代码访问，通常，它被用来保存那些需要在几个模块中共享的数据。比如通过luaL_newmetatable创建的元表就是放在全局的注册表中。
// seed：为了哈希的随机化的种子
// currentwhite：GC遍历的初始状态，默认为白色(代表待访问状态)， 其实还有一个颜色状态是"非当前白色"（otherwhite）,这两种白色交替使用。这是为了解决假如一个对象
// 在GC过程之的标记阶段之后创建，那它应该是白色，这样的话，这个对象就会被误删，所以增加了当前白色和非当前白色的概念，如果这个GC阶段是当前白色回收的话，那么下个GC阶段就是非当前白色回收
// ，依次轮回。lu_byte的定义在llimits.h中，是unsigned char
// gcstate：全局垃圾收集器的当前状态。分别有一下几种：GCSpause（暂停阶段）、GCSpropagate（传播阶段，用于遍历灰色节点检查对象的引用情况）、GCSsweepstring（字符串回收阶段）、
//           GCSsweep（回收阶段，用于对除了字符串之外的所有其他数据类型进行回收）和GCSfinalize（终止阶段）
// gckind：正在运行的GC种类
// gcrunning：如果GC正在运行则为true
// *allgc：存放待GC对象的列表，所有对象创建之后都会都会放入该链表中
// *sweepgc：待处理的回收数据都放在allgc中，由于回收阶段不是一次性全部回收这个链表的所有数据，所以使用这个变量来保存当前回收的位置，下一次从这个位置继续开始回收操作
// *finobj：带有析构器的回收对象的列表
// *gray：存放灰色节点链表(灰色代表当前对象为带扫描状态，表示该对象已经被GC访问过，但是该对象引用的其他对象还没有被访问到)
// *grayagain：存放需要一次性扫描处理的灰色节点链表，也就是说，这个链表上的所有数据的处理需要一步到位，不能被打断
// *weak：存放值是弱引用的table的链表
// *ephemeron：存放键是弱引用的table的链表
// *allweak：存放键值全是弱引用的table的链表
// *tobefnz：需要被GC的用户数据的列表
// *fixedgc：不需要被GC的对象列表
// *twups：带有upvalues的线程列表
// gcfinnum：每一步GC所调用的析构器数量
// gcpause：两次成功GC操作之间的间隔
// gcstepmul：控制GC的回收速度
// panic：出现无包含错误(unprotected errors)时，会调用这个函数。这个函数可以通过lua_atpanic来修改
// *mainthread：指向主lua_State，或者说是主线程、主执行栈。Lua虚拟机在调用函数lua_newstate初始化全局状态global_State时也会创建一个主线程
// *version：指向版本号的地址
// *memerrmsg：内存错误字符串，后面会对这个全局的错误消息字符串进行初始化
// *tmname[TM_N]：一些特殊方法的方法名数组
// *mt[LUA_NUMTAGS]：基础类型的元表数组
// *strcache[STRCACHE_N][STRCACHE_M]：缓存字符串数组
typedef struct global_State {
  lua_Alloc frealloc;  /* function to reallocate memory */
  void *ud;         /* auxiliary data to 'frealloc' */
  l_mem totalbytes;  /* number of bytes currently allocated - GCdebt */
  l_mem GCdebt;  /* bytes allocated not yet compensated by the collector */
  lu_mem GCmemtrav;  /* memory traversed by the GC */
  lu_mem GCestimate;  /* an estimate of the non-garbage memory in use */
  stringtable strt;  /* hash table for strings */
  TValue l_registry;
  unsigned int seed;  /* randomized seed for hashes */
  lu_byte currentwhite;
  lu_byte gcstate;  /* state of garbage collector */
  lu_byte gckind;  /* kind of GC running */
  lu_byte gcrunning;  /* true if GC is running */
  GCObject *allgc;  /* list of all collectable objects */
  GCObject **sweepgc;  /* current position of sweep in list */
  GCObject *finobj;  /* list of collectable objects with finalizers */
  GCObject *gray;  /* list of gray objects */
  GCObject *grayagain;  /* list of objects to be traversed atomically */
  GCObject *weak;  /* list of tables with weak values */
  GCObject *ephemeron;  /* list of ephemeron tables (weak keys) */
  GCObject *allweak;  /* list of all-weak tables */
  GCObject *tobefnz;  /* list of userdata to be GC */
  GCObject *fixedgc;  /* list of objects not to be collected */
  struct lua_State *twups;  /* list of threads with open upvalues */
  unsigned int gcfinnum;  /* number of finalizers to call in each GC step */
  int gcpause;  /* size of pause between successive GCs */
  int gcstepmul;  /* GC 'granularity' */
  lua_CFunction panic;  /* to be called in unprotected errors */
  struct lua_State *mainthread;
  const lua_Number *version;  /* pointer to version number */
  TString *memerrmsg;  /* memory-error message */
  TString *tmname[TM_N];  /* array with tag-method names */
  struct Table *mt[LUA_NUMTAGS];  /* metatables for basic types */
  TString *strcache[STRCACHE_N][STRCACHE_M];  /* cache for strings in API */
} global_State;


/*
** 'per thread' state
*/
// l_G:这个是Lua的全局对象，所有的Lua_State共享一个global_State. global_State是lua虚拟机的状态
// status： 一个thread实际上就是一个代码指令顺序执行的地方，也就是一个状态机，状态机执行的过程中会处于各种中间步骤，所以每个步骤都有一个status
// 可能出现的状态如下：(定义在lua.h中)
/* thread status */  
// #define LUA_OK      0  
// #define LUA_YIELD   1  
// #define LUA_ERRRUN  2  
// #define LUA_ERRSYNTAX   3  
// #define LUA_ERRMEM  4  
// #define LUA_ERRGCMM 5  
// #define LUA_ERRERR  6
// oldpc：一个thread的运行过程，就是一个解释执行指令的过程，必不可少的会有一个指针指向最后一次执行的指令的指针。
// StkId：是TValue *类型。一个thread的运行过程，需要两个基本的Stack。分别对应DataStack和CallStack。thread数据栈的栈底就是stack，栈顶是top，而top到stack_last之间则是未使用的部分。
// 本质上，对于一个栈，最重要的信息是：栈底，栈顶，栈空间（针对数据栈）
// stacksize：就是上面stack到stack_last之间的长度
// CallInfo：和数据栈直接用TValue数组存储不同，CallStack实际上是由CallInfo所构造成的链表，函数执行过程中，动态增减的CallInfo构成了一个链表。
// 同DataStack一样，我们需要记录这个CallStack的栈底：base_ci，由于lua是从宿主语言C开始发起调用的，栈底（最外层的CallInfo）base_ci一定是从C开始发起调用的。
// 而栈顶ci，就是当前正在执行的函数的CallInfo。
// nny、nCcalls：nCcalls记录的是CallStack动态增减过程中调用C函数的个数。而nny记录的是non-yieldable的调用个数

// *openupval、*twups：C Closure和Lua Closure都会有闭包变量。C Closure的闭包直接就是一个TValue数组保存在C Closure里，而Lua Closure的闭包分为open和close两种状态
// 如果是close状态，则也拷贝到LClosure自己的UpVal数组里，但如果是open状态，则直接指向了作用域上的变量地址。
// 可以理解，CallStack展开过程中，从CallStack的栈底到栈顶的所有open的UpVal也构成了一种Stack。
// Lua把这些open状态的UpVal用链表串在一起，我们可以认为是一个open upvalue stack，这个stack的栈底就是UpVal* openval
// 而一个lua_State代表一个协程，一个协程可能闭包别的协程的变量，所以struct lua_State *twups;就是代表了那些闭包了当前lua_State的变量的其他协程

// *errorJmp、errfunc：一个thread在CallStack执行过程中，需要有全局的异常、出错处理。
struct lua_State {
  CommonHeader;
  unsigned short nci;  /* number of items in 'ci' list */
  lu_byte status;
  StkId top;  /* first free slot in the stack */
  global_State *l_G;
  CallInfo *ci;  /* call info for current function */
  const Instruction *oldpc;  /* last pc traced */
  StkId stack_last;  /* last free slot in the stack */
  StkId stack;  /* stack base */
  UpVal *openupval;  /* list of open upvalues in this stack */
  GCObject *gclist;
  struct lua_State *twups;  /* list of threads with open upvalues */
  struct lua_longjmp *errorJmp;  /* current error recover point */
  CallInfo base_ci;  /* CallInfo for first level (C calling Lua) */
  volatile lua_Hook hook;
  ptrdiff_t errfunc;  /* current error handling function (stack index) */
  int stacksize;
  int basehookcount;
  int hookcount;
  unsigned short nny;  /* number of non-yieldable calls in stack */
  unsigned short nCcalls;  /* number of nested C calls */
  l_signalT hookmask;
  lu_byte allowhook;
};


// 取得lua_State的global_State变量
#define G(L)	(L->l_G)


/*
** Union of all collectable objects (only for conversions)
*/
// 此处的GCUnion是让GCObject转换成特定类型的联合体，因为在lobject.h中定义的GCObject只是一个没有特定类型的结构体，也仅仅只包含了类型和GC标记为
// 这里在这两种类型的基础上，增加了真实的数据类型
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
// 把GCObject对象转换成具体的类型的宏命令
// novariant的作用是获取特定类型的后四位(因为后四位决定了类型，前面的几位有别的用途，具体请参考lobject.h)

// gco2ts：将GCObject对象转换成string类型，返回地址
// gco2u：将GCObject对象转换成userdata类型，返回地址
// gco2lcl：将GCObject对象转换成light C function类型，返回地址
// gco2ccl：将GCObject对象转换成C closure类型，返回地址
// gco2cl：将GCObject对象转换成Lua closure类型，返回地址
// gco2ts：将GCObject对象转换成table类型，返回地址
// gco2p：将GCObject对象转换成Proto类型，返回地址
// gco2th：将GCObject对象转换成thread类型，返回地址
#define gco2ts(o)  \
	check_exp(novariant((o)->tt) == LUA_TSTRING, &((cast_u(o))->ts))
#define gco2u(o)  check_exp((o)->tt == LUA_TUSERDATA, &((cast_u(o))->u))
#define gco2lcl(o)  check_exp((o)->tt == LUA_TLCL, &((cast_u(o))->cl.l))
#define gco2ccl(o)  check_exp((o)->tt == LUA_TCCL, &((cast_u(o))->cl.c))
#define gco2cl(o)  \
	check_exp(novariant((o)->tt) == LUA_TFUNCTION, &((cast_u(o))->cl))
#define gco2t(o)  check_exp((o)->tt == LUA_TTABLE, &((cast_u(o))->h))
#define gco2p(o)  check_exp((o)->tt == LUA_TPROTO, &((cast_u(o))->p))
#define gco2th(o)  check_exp((o)->tt == LUA_TTHREAD, &((cast_u(o))->th))


/* macro to convert a Lua object into a GCObject */
  // 把一个lua对象转换成GCObject对象的宏
#define obj2gco(v) \
	check_exp(novariant((v)->tt) < LUA_TDEADKEY, (&(cast_u(v)->gc)))


/* actual number of total bytes allocated */
  // 返回分配的真实内存，g是global_State，总内存就是已经分配的内存加上垃圾收集器正在处理但是还没有释放的内存
#define gettotalbytes(g)	cast(lu_mem, (g)->totalbytes + (g)->GCdebt)

LUAI_FUNC void luaE_setdebt (global_State *g, l_mem debt);
LUAI_FUNC void luaE_freethread (lua_State *L, lua_State *L1);
LUAI_FUNC CallInfo *luaE_extendCI (lua_State *L);
LUAI_FUNC void luaE_freeCI (lua_State *L);
LUAI_FUNC void luaE_shrinkCI (lua_State *L);


#endif

