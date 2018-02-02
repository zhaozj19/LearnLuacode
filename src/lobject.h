/*
** $Id: lobject.h,v 2.117 2016/08/01 19:51:24 roberto Exp $
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
// LUA_TPROTO代表函数原型类型，由于这并不是一个公开类型，最终用户无法得到一个Proto类型，所以LUA_TPROTO没有定义在lua.h中，而存在于lobject.h中
// （LUA_TNONE排除在外，这个类型对lua不可见提供Lua API使用）
#define LUA_TPROTO	LUA_NUMTAGS		/* function prototypes */
// LUA_TDEADKEY用来标记lua数据的上限tag
#define LUA_TDEADKEY	(LUA_NUMTAGS+1)		/* removed keys in tables */

/*
** number of all possible tags (including LUA_TNONE but excluding DEADKEY)
*/
#define LUA_TOTALTAGS	(LUA_TPROTO + 2)


// 这里是对TValue里面的lu_byte类型tt做的解释
// tt是一个unsigned char数据
// 0-3表示数据的大类型（9种类型）
// 4-5表示（这个数据的扩展，比如说下面的长字符串、短字符串、浮点数、整数、lua闭包、c函数和c闭包这些数据扩展）
// 6表示是否是垃圾回收对象
/*
** tags for Tagged Values have the following use of bits:
** bits 0-3: actual tag (a LUA_T* value)
** bits 4-5: variant bits
** bit 6: whether value is collectable
*/


/*
** LUA_TFUNCTION variants:
** 0 - Lua function
** 1 - light C function
** 2 - regular C function (closure)
*/

// 这个地方是对9中Lua类型的tag位（4和5）进行设置
// 比如说LUA_TFUNCTION这个类型，45位为0时代表lua闭包，为1时代表C函数，为2时代表C闭包
// 所以下面的操作是对tag位进行扩展的

/* Variant tags for functions */
#define LUA_TLCL	(LUA_TFUNCTION | (0 << 4))  /* Lua closure */
#define LUA_TLCF	(LUA_TFUNCTION | (1 << 4))  /* light C function */
#define LUA_TCCL	(LUA_TFUNCTION | (2 << 4))  /* C closure */


/* Variant tags for strings */
#define LUA_TSHRSTR	(LUA_TSTRING | (0 << 4))  /* short strings */
#define LUA_TLNGSTR	(LUA_TSTRING | (1 << 4))  /* long strings */


/* Variant tags for numbers */
#define LUA_TNUMFLT	(LUA_TNUMBER | (0 << 4))  /* float numbers */
#define LUA_TNUMINT	(LUA_TNUMBER | (1 << 4))  /* integer numbers */


// 这个是看tag的第六位是不是1，是1的话就属于垃圾回收，否则就不需要关心它的生命周期
/* Bit mark for collectable types */
#define BIT_ISCOLLECTABLE	(1 << 6)


// 把传过来的tag值的第六位进行标记垃圾回收
/* mark a tag as collectable */
#define ctb(t)			((t) | BIT_ISCOLLECTABLE)


/*
** Common type for all collectable objects
*/
// GCObject是lua里面所有需要垃圾回收对象的联合体
typedef struct GCObject GCObject;


/*
** Common Header for all collectable objects (in macro form, to be
** included in other objects)
*/
// 所有需要GC操作的数据都会加一个CommonHeader类型的宏定义
// next指向下一个GC链表的数据
// tt代表数据的类型以及扩展类型以及GC位的标志
// marked是执行GC的标记为，用于具体的GC算法
// marked用于存储GC需要的颜色的。具体定义如下：
// #define WHITE0BIT 0  /* object is white (type 0) */
// #define WHITE1BIT 1  /* object is white (type 1) */ 
// #define BLACKBIT  2  /* object is black */
// #define FINALIZEDBIT  3  /* object has been marked for finalization */

#define CommonHeader	GCObject *next; lu_byte tt; lu_byte marked


/*
** Common type has only the common header
*/
struct GCObject {
  CommonHeader;
};




/*
** Tagged Values. This is the basic representation of values in Lua,
** an actual value plus a tag with its type.
*/

/*
** Union of all Lua values
*/
// 然后GCObject和其他不需要进行进行GC的数据放在一个联合体里面构成了Value类型
// 在lua中，一共有9种数据类型，分别为nil、boolean、lightuserdata、number、string、table、function、userdata和thread
// 其中，只有string、table、function、thread四种在vm中以引用方式共享，是需要被GC管理回收的对象。其他类型都是以值的形式存在
typedef union Value {
  GCObject *gc;    /* collectable objects */
  void *p;         /* light userdata */
  int b;           /* booleans */
  lua_CFunction f; /* light C functions */
  lua_Integer i;   /* integer numbers */
  lua_Number n;    /* float numbers */
} Value;


// 最后在Value的基础上又加上了tt_来标记数据到底是什么类型的
#define TValuefields	Value value_; int tt_


// TValue就是最终的数据结构
typedef struct lua_TValue {
  TValuefields;
} TValue;



/* macro defining a nil value */
#define NILCONSTANT	{NULL}, LUA_TNIL


// 获取TValue的Value部分
#define val_(o)		((o)->value_)

// 获取TValue的tt_部分
/* raw type tag of a TValue */
#define 
rttype(o)	((o)->tt_)

// 用来获取tag的后四位
/* tag with no variants (bits 0-3) */
#define novariant(x)	((x) & 0x0F)

// 用来获取tag的后六位
/* type tag of a TValue (bits 0-3 for tags + variant bits 4-5) */
#define ttype(o)	(rttype(o) & 0x3F)

/* type tag of a TValue with no variants (bits 0-3) */
#define ttnov(o)	(novariant(rttype(o)))


// 下面这些宏是用来判断o的tag是不是满足条件
// 比如说是否是float、int、table等等
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


// check_exp是一个断言，定义在limits.h中
// #define check_exp(c,e)    (lua_assert(c), (e))
/* Macros to access values */
#define ivalue(o)	check_exp(ttisinteger(o), val_(o).i)
#define fltvalue(o)	check_exp(ttisfloat(o), val_(o).n)
#define nvalue(o)	check_exp(ttisnumber(o), \
	(ttisinteger(o) ? cast_num(ivalue(o)) : fltvalue(o)))
#define gcvalue(o)	check_exp(iscollectable(o), val_(o).gc)
#define pvalue(o)	check_exp(ttislightuserdata(o), val_(o).p)
#define tsvalue(o)	check_exp(ttisstring(o), gco2ts(val_(o).gc))
#define uvalue(o)	check_exp(ttisfulluserdata(o), gco2u(val_(o).gc))
#define clvalue(o)	check_exp(ttisclosure(o), gco2cl(val_(o).gc))
#define clLvalue(o)	check_exp(ttisLclosure(o), gco2lcl(val_(o).gc))
#define clCvalue(o)	check_exp(ttisCclosure(o), gco2ccl(val_(o).gc))
#define fvalue(o)	check_exp(ttislcf(o), val_(o).f)
#define hvalue(o)	check_exp(ttistable(o), gco2t(val_(o).gc))
#define bvalue(o)	check_exp(ttisboolean(o), val_(o).b)
#define thvalue(o)	check_exp(ttisthread(o), gco2th(val_(o).gc))
/* a dead value may get the 'gc' field, but cannot access its contents */
#define deadvalue(o)	check_exp(ttisdeadkey(o), cast(void *, val_(o).gc))

#define l_isfalse(o)	(ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))


#define iscollectable(o)	(rttype(o) & BIT_ISCOLLECTABLE)


/* Macros for internal tests */
#define righttt(obj)		(ttype(obj) == gcvalue(obj)->tt)


// 检查obj的生存期
// iscollectable(obj)检查obj是否为GC对象
// righttt(obj)返回obj的tt_是否等于gc里面的tt
// isdead(obj)返回obj是否已经被清理
// 总而言之，返回true代表未被GC的和不需要GC的，返回false代表已经被GC了的
#define checkliveness(L,obj) \
	lua_longassert(!iscollectable(obj) || \
		(righttt(obj) && (L == NULL || !isdead(G(L),gcvalue(obj)))))


// 在lua里面的值是无关类型的，就是靠下面的操作来让变量类型相互变化
// 通过改变TValue的tt_和Value里面的具体值（比如：i,n,f,b等）
// set函数是设置新的值，改变的有tt_
  // chg函数也是改变新的值，但是没有改变tt_

    // 不需要GC的值复制操作 直接替换Value和tt，需要GC的值复制操作还要检查一下生存期
/* Macros to set values */
#define settt_(o,t)	((o)->tt_=(t))

#define setfltvalue(obj,x) \
  { TValue *io=(obj); val_(io).n=(x); settt_(io, LUA_TNUMFLT); }

#define chgfltvalue(obj,x) \
  { TValue *io=(obj); lua_assert(ttisfloat(io)); val_(io).n=(x); }

#define setivalue(obj,x) \
  { TValue *io=(obj); val_(io).i=(x); settt_(io, LUA_TNUMINT); }

#define chgivalue(obj,x) \
  { TValue *io=(obj); lua_assert(ttisinteger(io)); val_(io).i=(x); }

#define setnilvalue(obj) settt_(obj, LUA_TNIL)

#define setfvalue(obj,x) \
  { TValue *io=(obj); val_(io).f=(x); settt_(io, LUA_TLCF); }

#define setpvalue(obj,x) \
  { TValue *io=(obj); val_(io).p=(x); settt_(io, LUA_TLIGHTUSERDATA); }

#define setbvalue(obj,x) \
  { TValue *io=(obj); val_(io).b=(x); settt_(io, LUA_TBOOLEAN); }

#define setgcovalue(L,obj,x) \
  { TValue *io = (obj); GCObject *i_g=(x); \
    val_(io).gc = i_g; settt_(io, ctb(i_g->tt)); }

#define setsvalue(L,obj,x) \
  { TValue *io = (obj); TString *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(x_->tt)); \
    checkliveness(L,io); }

#define setuvalue(L,obj,x) \
  { TValue *io = (obj); Udata *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TUSERDATA)); \
    checkliveness(L,io); }

#define setthvalue(L,obj,x) \
  { TValue *io = (obj); lua_State *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TTHREAD)); \
    checkliveness(L,io); }

#define setclLvalue(L,obj,x) \
  { TValue *io = (obj); LClosure *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TLCL)); \
    checkliveness(L,io); }

#define setclCvalue(L,obj,x) \
  { TValue *io = (obj); CClosure *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TCCL)); \
    checkliveness(L,io); }

#define sethvalue(L,obj,x) \
  { TValue *io = (obj); Table *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TTABLE)); \
    checkliveness(L,io); }

#define setdeadvalue(obj)	settt_(obj, LUA_TDEADKEY)



#define setobj(L,obj1,obj2) \
	{ TValue *io1=(obj1); *io1 = *(obj2); \
	  (void)L; checkliveness(L,io1); }


/*
** different types of assignments, according to destination
*/


/* from stack to (same) stack */
#define setobjs2s	setobj
/* to stack (not from same stack) */
#define setobj2s	setobj
#define setsvalue2s	setsvalue
#define sethvalue2s	sethvalue
#define setptvalue2s	setptvalue
/* from table to same table */
#define setobjt2t	setobj
/* to new object */
#define setobj2n	setobj
#define setsvalue2n	setsvalue

/* to table (define it as an expression to be used in macros) */
#define setobj2t(L,o1,o2)  ((void)L, *(o1)=*(o2), checkliveness(L,(o1)))




/*
** {======================================================
** types and prototypes
** =======================================================
*/

typedef TValue *StkId;  /* index to stack elements */




/*
** Header for string value; string bytes follow the end of this structure
** (aligned according to 'UTString'; see next).
*/
// TString是一个lua字符串的数据结构
    // CommonHeader是一个需要GC对象的头文件
    // extra用于标记是否是虚拟机保留的字符串，如果这个值为1，那么不会GC（保留字符串即是lua中的关键字）
    // shrlen保留字符串的长度(因为lua并不以\0结尾来识别字符串的长度，故需要一个len域来记录其长度)
    // 字符串的hash值（lua字符串的比较是用字符串的hash值来比较的）
    // 下面的union联合体，lnglen表示长字符的长度
    // hnext代表链接下一个字符串

typedef struct TString {
  CommonHeader;
  lu_byte extra;  /* reserved words for short strings; "has hash" for longs */
  lu_byte shrlen;  /* length for short strings */
  unsigned int hash;
  union {
    size_t lnglen;  /* length for long strings */
    struct TString *hnext;  /* linked list for hash table */
  } u;
} TString;


/*
** Ensures that address after this type is always fully aligned.
*/
// L_Umaxalign是一个LUAI_USER_ALIGNMENT_T的宏，这是一个联合体里面最大值的字节数量，用来保证UTString结构里面的TString按照这个长度来对齐
typedef union UTString {
  L_Umaxalign dummy;  /* ensures maximum alignment for strings */
  TString tsv;
} UTString;


/*
** Get the actual string (array of bytes) from a 'TString'.
** (Access to 'extra' ensures that value is really a 'TString'.)
*/
// 获得实际的字符串（也就是上面的结构体）
#define getstr(ts)  \
  check_exp(sizeof((ts)->extra), cast(char *, (ts)) + sizeof(UTString))


/* get the actual string (array of bytes) from a Lua value */
#define svalue(o)       getstr(tsvalue(o))

/* get string length from 'TString *s' */
#define tsslen(s)	((s)->tt == LUA_TSHRSTR ? (s)->shrlen : (s)->u.lnglen)

/* get string length from 'TValue *o' */
#define vslen(o)	tsslen(tsvalue(o))


/*
** Header for userdata; memory area follows the end of this structure
** (aligned according to 'UUdata'; see next).
*/

// UserData在lua中和string类似，可以看成是拥有独立元表，不被内部化，也不需要追加\0的字符串
  // 因为UserData也是需要GC的，所以这里加上CommonHeader
  // ttuv_用来标记这个数据是UserData数据
  // 因为UserData数据拥有独立元表 所以这里多了一个*metatabel的数据
  // len用于记录UserData长度
  // user_参考上面对Value联合体的解析
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
// dummy的含义请参考上述字符串
// userdata分为LUA_TLIGHTUSERDATA和LUA_TUSERDATA，其中前一种类型即为轻量级userdata（light userdata），
// 轻量级userdata是一种表示C指针的值，对Lua虚拟机来说，这种数据类型不需要GC（垃圾回收），其指向的内存由用户分配和释放；
// 后一种userdata类型完全userdata（full userdata），内存是由Lua虚拟机分配，并有GC机制负责处理。
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

// 对Udata数据进行赋值的操作，重点及时替换掉Udata结构体里面的user_和ttuv_标记（rttype来返回数据的类型）
#define setuservalue(L,u,o) \
	{ const TValue *io=(o); Udata *iu = (u); \
	  iu->user_ = io->value_; iu->ttuv_ = rttype(io); \
	  checkliveness(L,io); }

// getuservalue把u的值赋给o
// settt_(o,t)更改o的类型为t
#define getuservalue(L,u,o) \
	{ TValue *io=(o); const Udata *iu = (u); \
	  io->value_ = iu->user_; settt_(io, iu->ttuv_); \
	  checkliveness(L,io); }


/*
** Description of an upvalue for function prototypes
*/
    // 用来记录upvalue的调试信息
    // name记录名字
    // instack记录是否在堆栈中
    // idx记录这个upvalue在函数列表中的索引
typedef struct Upvaldesc {
  TString *name;  /* upvalue name (for debug information) */
  lu_byte instack;  /* whether it is in stack (register) */
  lu_byte idx;  /* index of upvalue (in stack or in outer function's list) */
} Upvaldesc;


/*
** Description of a local variable for function prototypes
** (used for debug information)
*/
// 用来记录函数内部变量的调试信息
// *varname记录名字
// startpc和endpc分别记录生成和回收的代码点
typedef struct LocVar {
  TString *varname;
  int startpc;  /* first point where variable is active */
  int endpc;    /* first point where variable is dead */
} LocVar;


/*
** Function Prototypes
*/
// 函数原型数据结构
// CommonHeader是一个需要GC对象的头文件
// numparams用来记录固定参数的个数
// is_vararg是否支持变参。1表示使用了变参作为最后一个参数
// maxstacksize记录调用这个函数需要多大的数据栈空间
// sizeupvalues记录这个函数原型的upvalue的数量
// sizek记录常量表的大小
// sizecode记录字节码的数量
// code用来记录字节码数组
// **p记录函数原型的函数原型（因为lua中函数体里面可以定义函数，所以必然存在函数原型内存在函数原型）
// cache的作用是用来引用生成的闭包，下次再通过这个原型生成闭包时，比较upvalue是否一致来决定复用。cache是一个弱引用，一旦在gc流程中发现引用的闭包不存在，cache将被置空
// source用来记录调试信息数组。lua的调试信息很丰富。每条code中的指令都对应着line数组中的源代码行号。局部变量好upvalue的调试信息，包括名字和在源代码中的作用域都记录在Proto结构里。
// *gclist记录Proto内需要GC的对象
// *locvars记录函数内部变量的调试信息
// *upvalues记录upvalue的调试信息
typedef struct Proto {
  CommonHeader;
  lu_byte numparams;  /* number of fixed parameters */
  lu_byte is_vararg;
  lu_byte maxstacksize;  /* number of registers needed by this function */
  int sizeupvalues;  /* size of 'upvalues' */
  int sizek;  /* size of 'k' */
  int sizecode;
  int sizelineinfo;
  int sizep;  /* size of 'p' */
  int sizelocvars;
  int linedefined;  /* debug information  */
  int lastlinedefined;  /* debug information  */
  TValue *k;  /* constants used by the function */
  Instruction *code;  /* opcodes */
  struct Proto **p;  /* functions defined inside the function */
  int *lineinfo;  /* map from opcodes to source lines (debug information) */
  LocVar *locvars;  /* information about local variables (debug information) */
  Upvaldesc *upvalues;  /* upvalue information */
  struct LClosure *cache;  /* last-created closure with this prototype */
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

// 闭包包含的通用部分
// CommonHeader作为GC部分的表示，nupvalues是upvalues的数量，类型是unsigned char，为什么也会有*gclist呐？
#define ClosureHeader \
	CommonHeader; lu_byte nupvalues; GCObject *gclist

typedef struct CClosure {
  ClosureHeader;
  lua_CFunction f;
  TValue upvalue[1];  /* list of upvalues */
} CClosure;


// lua闭包的定义：
// Proto指针：用于保存分析阶段生成的字节码等信息
// UpVal指针：用于保存这个Closure相关的UpValue
// tips:在lua中，函数是第一类值，这意味着定义函数和其他普通类型是一样的，区别在于函数对应的数据值是对应的函数体指令罢了。
// 因此，以前所谓的"相同函数"，如果更严格地说，应该指的是使用同一份函数代码，即同一个Proto指针，但是它们的UpValue并不见得一样，而UpValue正是函数执行时的环境信息之一
// 严格的说，Lua中 只有闭包而不存在"函数"，因为函数只是一种特殊的闭包
typedef struct LClosure {
  ClosureHeader;
  struct Proto *p;
  UpVal *upvals[1];  /* list of upvalues */
} LClosure;


typedef union Closure {
  CClosure c;
  LClosure l;
} Closure;


#define isLfunction(o)	ttisLclosure(o)

#define getproto(o)	(clLvalue(o)->p)


/*
** Tables
*/
// 在任何时候TKey的类型只有两个，要么是整数，要么不是整数（非nil）
// next在之前的版本是 struct Node *next; 类型的，5.3换成了int类型，是一个偏移。next指向冲突的下一个节点
typedef union TKey {
  struct {
    TValuefields;
    int next;  /* for chaining (offset for next node) */
  } nk;
  TValue tvk;
} TKey;


/* copy a value into a key without messing up field 'next' */
#define setnodekey(L,key,obj) \
	{ TKey *k_=(key); const TValue *io_=(obj); \
	  k_->nk.value_ = io_->value_; k_->nk.tt_ = io_->tt_; \
	  (void)L; checkliveness(L,io_); }



// 此自定义的Node节点就是table的节点值
typedef struct Node {
  TValue i_val;
  TKey i_key;
} Node;


// flags表示此table中存在哪些元方法，默认是0。元方法对应的bit定义在ltm.h中
// lsizenode是该表中以2为底的散列表大小的对数值。
// sizearray是数组部分的大小
// *array指向数组部分的指针
// *node指向散列表起始位置的指针
// *lastfree指向散列表最后位置的下一个指针，就是哈希部分的下一个指针
// *metatable存放该表的元表
// *gclist：GC相关的链表
typedef struct Table {
  CommonHeader;
  lu_byte flags;  /* 1<<p means tagmethod(p) is not present */
  lu_byte lsizenode;  /* log2 of size of 'node' array */
  unsigned int sizearray;  /* size of 'array' array */
  TValue *array;  /* array part */
  Node *node;
  Node *lastfree;  /* any free position is before this position */
  struct Table *metatable;
  GCObject *gclist;
} Table;



/*
** 'module' operation for hashing (size is always a power of 2)
*/
// s是传进来的key中的具体值，size是哈希部分的大小
// (size&(size-1))==0  首先判断传进来的size是不是2的幂是的话 ，就返回后面的内容
// (s) & ((size)-1)  计算出一个节点的i值，然后再根据i值来判断节点应该所在的位置
// 至于这个地方为什么是(s) & ((size)-1)来计算，我就不明白了
#define lmod(s,size) \
	(check_exp((size&(size-1))==0, (cast(int, (s) & ((size)-1)))))


#define twoto(x)	(1<<(x))
#define sizenode(t)	(twoto((t)->lsizenode))


/*
** (address of) a fixed nil value
*/
#define luaO_nilobject		(&luaO_nilobject_)


LUAI_DDEC const TValue luaO_nilobject_;

/* size of buffer for 'luaO_utf8esc' function */
#define UTF8BUFFSZ	8

LUAI_FUNC int luaO_int2fb (unsigned int x);
LUAI_FUNC int luaO_fb2int (int x);
LUAI_FUNC int luaO_utf8esc (char *buff, unsigned long x);
LUAI_FUNC int luaO_ceillog2 (unsigned int x);
LUAI_FUNC void luaO_arith (lua_State *L, int op, const TValue *p1,
                           const TValue *p2, TValue *res);
LUAI_FUNC size_t luaO_str2num (const char *s, TValue *o);
LUAI_FUNC int luaO_hexavalue (int c);
LUAI_FUNC void luaO_tostring (lua_State *L, StkId obj);
LUAI_FUNC const char *luaO_pushvfstring (lua_State *L, const char *fmt,
                                                       va_list argp);
LUAI_FUNC const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);
LUAI_FUNC void luaO_chunkid (char *out, const char *source, size_t len);


#endif

