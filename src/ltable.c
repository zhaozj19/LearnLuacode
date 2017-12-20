/*
** $Id: ltable.c,v 2.118 2016/11/07 12:38:35 roberto Exp $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/

#define ltable_c
#define LUA_CORE

#include "lprefix.h"


/*
** Implementation of tables (aka arrays, objects, or hash tables).
** Tables keep its elements in two parts: an array part and a hash part.
** Non-negative integer keys are all candidates to be kept in the array
** part. The actual size of the array is the largest 'n' such that
** more than half the slots between 1 and n are in use.
** Hash uses a mix of chained scatter table with Brent's variation.
** A main invariant of these tables is that, if an element is not
** in its main position (i.e. the 'original' position that its hash gives
** to it), then the colliding element is in its own main position.
** Hence even when the load factor reaches 100%, performance remains good.
*/
// table的实例（数组，对象，或哈希部分）
// table保持它们的元素在两个部分：数组部分和哈希部分
// 非负整数保存在数组部分
#include <math.h>
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
// 数组部分的最大size是2^MAXABITS. MAXABITS是最大的整数，MAXASIZE是对于unsigned int而言的
// CHAR_BIT就是char的位数，定义在C的<limits.h>中
// 总结：MAXABITS是数组size的以2为底的对数
      // MAXASIZE就是数组的size
#define MAXABITS	cast_int(sizeof(int) * CHAR_BIT - 1)
#define MAXASIZE	(1u << MAXABITS)

/*
** Maximum size of hash part is 2^MAXHBITS. MAXHBITS is the largest
** integer such that 2^MAXHBITS fits in a signed int. (Note that the
** maximum number of elements in a table, 2^MAXABITS + 2^MAXHBITS, still
** fits comfortably in an unsigned int.)
*/
// 哈希部分的最大size是2^MAXHBITS. MAXHBITS是最大的整数，2^MAXHBITS是对于signed int而言的
// table里面最大的元素数量是 2^MAXABITS + 2^MAXHBITS
// 类型是unsigned int
#define MAXHBITS	(MAXABITS - 1)

// #define gnode(t,i)  (&(t)->node[i])
// gnode的定义如上，用来返回t的哈希部分的第i个节点，此处的i就是哈希部分0-size-1的整数值
// 这里的sizenode(t)是返回t的哈希部分的尺寸
// 然后再调用lmod求出node所在位置的i值（lmod函数是最重要的）
// 再调用gnode来取得node节点
#define hashpow2(t,n)		(gnode(t, lmod((n), sizenode(t))))


// 通过str结构体的hash字段求出哈希值，原理和int一样
// 这个hash就是字符串的hash值
#define hashstr(t,str)		hashpow2(t, (str)->hash)

// 通过传进来的p,再调用hashpow2求出哈希值,这里的p是一个int数值
#define hashboolean(t,p)	hashpow2(t, p)


// int类型的key求哈希值
#define hashint(t,i)		hashpow2(t, i)


/*
** for some types, it is better to avoid modulus by power of 2, as
** they tend to have many 2 factors.
*/
#define hashmod(t,n)	(gnode(t, ((n) % ((sizenode(t)-1)|1))))

// point2uint把指针转换为int数据
#define hashpointer(t,p)	hashmod(t, point2uint(p))


// 宏定义一个虚拟节点，用于空哈希部分的元素
#define dummynode		(&dummynode_)

// 此Node数据结构是lobject.h中定义的table的哈希部分节点的数据结构
// value部分是TValue类型，NILCONSTANT的定义为  #define NILCONSTANT {NULL}, LUA_TNIL
// key部分是TKey类型。nk->TValuefields = NILCONSTANT  nk->next = 0
static const Node dummynode_ = {
  {NILCONSTANT},  /* value */
  {{NILCONSTANT, 0}}  /* key */
};


/*
** Hash for floating-point numbers.
** The main computation should be just
**     n = frexp(n, &i); return (n * INT_MAX) + i
** but there are some numerical subtleties.
** In a two-complement representation, INT_MAX does not has an exact
** representation as a float, but INT_MIN does; because the absolute
** value of 'frexp' is smaller than 1 (unless 'n' is inf/NaN), the
** absolute value of the product 'frexp * -INT_MIN' is smaller or equal
** to INT_MAX. Next, the use of 'unsigned int' avoids overflows when
** adding 'i'; the use of '~u' (instead of '-u') avoids problems with
** INT_MIN.
*/
// 浮点型数据的哈希算法
// 主要的计算应该被调整为 n = frexp(n, &i);  return (n * INT_MAX) + i
// 但也有一些数字上的微妙之处。
// 在一个两个组合的表示中，INT_MAX没有一个精确的浮点数表示。但是INT_MAX有
// 因为frexp的绝对值是一个比一小的数字（除非n是无穷大或无效数字）
// 'frexp * -INT_MIN'的绝对值小于或等于INT_MAX。通过使用unsigned int可以避免加i时溢出，通过使用~u可以避免INT_MIN的问题

// frexp是一个C语言函数，功能是把一个浮点数分解为尾数和指数
// 第一个参数是要分解的浮点数据，第二个参数是存储指数的指针
// 返回的是尾数
// 然后拿着返回的尾数来乘以-INT_MIN,结果用n保存
// 下面判断n能不能转换为int,不能的话就报错
// 可以的话就用unsigned int类型的u来保存ni加上之前的指数
// 如果u超出了INT_MAX就返回~u,不然直接u
#if !defined(l_hashfloat)
static int l_hashfloat (lua_Number n) {
  int i;
  lua_Integer ni;
  n = l_mathop(frexp)(n, &i) * -cast_num(INT_MIN);
  if (!lua_numbertointeger(n, &ni)) {  /* is 'n' inf/-inf/NaN? */
    lua_assert(luai_numisnan(n) || l_mathop(fabs)(n) == cast_num(HUGE_VAL));
    return 0;
  }
  else {  /* normal case */
    unsigned int u = cast(unsigned int, i) + cast(unsigned int, ni);
    return cast_int(u <= cast(unsigned int, INT_MAX) ? u : ~u);
  }
}
#endif


/*
** returns the 'main' position of an element in a table (that is, the index
** of its hash value)
*/
// 返回这个key的mainposition
// ttype返回key的tt
// 对于int类型的哈希
  // ivalue(key)可以获取到key的i字段，这个字段是int数据的真实值，然后再调用hashint
// 对于float类型的哈希
  // fltvalue(key)返回浮点型数据的n，定义如下： lua_Number n;    /* float numbers */
  // 再调用l_hashfloat求出计算出的u值，来让hashmod求出哈希值
// 对于short string的哈希值(短字符串就是系统关键字)
  // tsvalue(key)求出key的tsv字段，然后再调用hashstr求出哈希值、
// 对于long string的哈希值
  // tsvalue(key)求出key的tsv字段,luaS_hashlongstr也是用来返回哈希值的,有点小区别,到string的时候再分析
// 对于boolean的哈希值
  // bvalue求出key的b字段,其实也是个int类型,然后通过hashboolean求出哈希值
// 对于用户自定义数据的哈希值
  // hashpointer把key的指针转换为int,然后和int求哈希的方法一样,求出pointer的哈希值
// 对于C函数的哈希值
  // 也是根据hashpointer来求,只不过传递的是f字段  lua_CFunction f; /* light C functions */
// 对于别的来说,首先判断是不是已经无效的key,如果有效就通过gc字段来求哈希值
// ttisdeadkey的操作是根据key的tt字段的设置来判断是不是为LUA_TDEADKEY
static Node *mainposition (const Table *t, const TValue *key) {
  switch (ttype(key)) {
    case LUA_TNUMINT:
      return hashint(t, ivalue(key));
    case LUA_TNUMFLT:
      return hashmod(t, l_hashfloat(fltvalue(key)));
    case LUA_TSHRSTR:
      return hashstr(t, tsvalue(key));
    case LUA_TLNGSTR:
      return hashpow2(t, luaS_hashlongstr(tsvalue(key)));
    case LUA_TBOOLEAN:
      return hashboolean(t, bvalue(key));
    case LUA_TLIGHTUSERDATA:
      return hashpointer(t, pvalue(key));
    case LUA_TLCF:
      return hashpointer(t, fvalue(key));
    default:
      lua_assert(!ttisdeadkey(key));
      return hashpointer(t, gcvalue(key));
  }
}


/*
** returns the index for 'key' if 'key' is an appropriate key to live in
** the array part of the table, 0 otherwise.
*/
// 如果key是一个在数组部分的合适的key（就是要确保key的值是正整数才能在数组部分）那么就返回key的索引，否则返回0
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
// 返回表遍历的key的索引。首先从数组部分开始遍历，然后才是哈希部分。遍历是由0开始的
// 此处的i保存的就是key对应的值（对于正整数而言就是它本身的值，其他的话就是0）
// 然后就根据i的值，来判断是否在数组部分，是的话就直接返回i
// 如果是在哈希部分的话，就先找出和这个key对应的mp
// luaV_rawequalobj比较两个obj是否内存意义上相等，或者看这个key是不是已经被回收了
// 满足任何一个都可以得到key对应的i，这个i是key在hash上的索引
// 然后返回(i + 1) + t->sizearray
// 如果luaV_rawequalobj返回false的话，就说明这个key在mp的链表上，然后遍历链表继续查找
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

// luaH_next实现table的递归。通过上一个键，来找到下一个键值对。
// 首先通过findindex找到key对应的index（对于数组部分，就是对应的key值;哈希部分的话，就是next对应的整数值（偏差））
// 找到这个i之后，把i+1，就是下一个元素index，然后会把&t->array[i]放入栈顶
int luaH_next (lua_State *L, Table *t, StkId key) {
  unsigned int i = findindex(L, t, key);  /* find original element */
  for (; i < t->sizearray; i++) {  /* try first array part */
    if (!ttisnil(&t->array[i])) {  /* a non-nil value? */
      setivalue(key, i + 1);
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
** between 2^(i - 1) + 1 and 2^i. 'pna' enters with the total number of
** integer keys in the table and leaves with the number of keys that
** will go to the array part; return the optimal size.
*/
// 计算t中最理想的数组大小。nums数组中第i个元素存放的是table中key在2的i-1次幂和2的i次幂之间的元素数量。
// pna是table中正整数的数量
// 遍历这个nums数组，获得其范围区间内所包含的整数数量大于50%的最大索引，作为重新哈希之后的数组大小，超过这个范围的正整数，就分配到哈希部分了
static unsigned int computesizes (unsigned int nums[], unsigned int *pna) {
  int i;
  unsigned int twotoi;  /* 2^i (candidate for optimal size) */
  unsigned int a = 0;  /* number of elements smaller than 2^i */
  unsigned int na = 0;  /* number of elements to go to array part */
  unsigned int optimal = 0;  /* optimal size for array part */
  /* loop while keys can fill more than half of total size */
  for (i = 0, twotoi = 1; *pna > twotoi / 2; i++, twotoi *= 2) {
    if (nums[i] > 0) {
      a += nums[i];
      if (a > twotoi/2) {  /* more than half elements present? */
        optimal = twotoi;  /* optimal size (till now) */
        na = a;  /* all elements up to 'optimal' will go to array part */
      }
    }
  }
  lua_assert((optimal == 0 || optimal / 2 < na) && na <= optimal);
  *pna = na;
  return optimal;
}

// arrayindex返回key的索引，返回的值不等于0才算key在t的数组部分
// luaO_ceillog2也就是求出k的以2为底的对数，在对结果进行向上取整
// 总的来说，countint的作用就是对nums的某些位置进行计算
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
** Count keys in array part of table 't': Fill 'nums[i]' with
** number of keys that will go into corresponding slice and return
** total number of non-nil keys.
*/
// numusearray函数遍历表中的数组部分，计算其中的元素数量，并更新对应的nums数组中的元素数量
// 里面的操作基本大意是，找出数组部分的2^(lg - 1), 2^lg之间的元素数量，然后对nums的对应格子进行赋值
// 然后记录下来array的数量并且返回
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

// 计算t中哈希部分的元素数量
// 因为其中也可能存放了正整数，需要根据这里的正整数数量更新对应的nums数组元素数量
static int numusehash (const Table *t, unsigned int *nums, unsigned int *pna) {
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
  *pna += ause;
  return totaluse;
}

// 对表的数组部分大小进行设置
// 函数很简单先申请出来size个节点的内存空间,然后在循环里面对size个元素进行初始化设置,主要操作就是把array部分的每一个节点都设置为NULL
// 然后把数组部分长度的sizearray字段设置为size就好了
// luaM_reallocvector这个函数是在lmem.h中定义的，到时候在分析
static void setarrayvector (lua_State *L, Table *t, unsigned int size) {
  unsigned int i;
  luaM_reallocvector(L, t->array, t->sizearray, size, TValue);
  for (i=t->sizearray; i<size; i++)
     setnilvalue(&t->array[i]);
  t->sizearray = size;
}


// 对表的哈希部分大小进行设置
// 如果传进来的size为0，那么t的node(指向散列表起始位置的指针)是指一个虚拟节点
// lsizenode是该表中以2为底的散列表大小的对数值，此处设置为0
// lastfree指向散列表最后位置的指针，此处设置为NULL

// 如果size不为0，那么久根据传入的size来计算哈希部分的lsizenode值（luaO_ceillog2是计算出size的以2为底的对数）
// 如果size大于MAXHBITS则报错。MAXHBITS是允许哈希部分最大的2的幂
// 然后twoto的意思为求出lsize的2^lsize的结果  定义为#define twoto(x)  (1<<(x))
// 设置完size之后 下面就开始申请哈希部分的空间了，让node指向新空间的起始地址
// 然后在循环里面对size个元素进行设置
// gnode用来返回t的哈希部分的第i个节点
// gnext用来返回此节点的下一个节点的地址（其实是一个偏移）
// #define wgkey(n)    (&(n)->i_key.nk)
// #define gval(n)   (&(n)->i_val)
// setnilvalue(wgkey(n)); 让n的i_key.nk字段设为NULL
// setnilvalue(gval(n));  让n的i_val字段设为NULL
// 然后再对t的lsizenode和lastfree设置就完成了哈希部分的初始化
// 此处lastfree指向的是size节点，也就是哈希部分的下一个节点
static void setnodevector (lua_State *L, Table *t, unsigned int size) {
  if (size == 0) {  /* no elements to hash part? */
    t->node = cast(Node *, dummynode);  /* use common 'dummynode' */
    t->lsizenode = 0;
    t->lastfree = NULL;  /* signal that it is using dummy node */
  }
  else {
    int i;
    int lsize = luaO_ceillog2(size);
    if (lsize > MAXHBITS)
      luaG_runerror(L, "table overflow");
    size = twoto(lsize);
    t->node = luaM_newvector(L, size, Node);
    for (i = 0; i < (int)size; i++) {
      Node *n = gnode(t, i);
      gnext(n) = 0;
      setnilvalue(wgkey(n));
      setnilvalue(gval(n));
    }
    t->lsizenode = cast_byte(lsize);
    t->lastfree = gnode(t, size);  /* all positions are free */
  }
}


// 首先保存一下之前的oldasize，oldhsize（allocsizenode返回以2为底的散列表大小的对数值）
// 然后判断大小做出相应的逻辑，这里要注意一下，如果(nasize < oldasize)，则会把多出来的数组位置的value设为nil（调用luaH_setint），然后收缩数组部分
// 对于哈希部分来说，从后面向前面遍历，重新插入一下哈希部分
void luaH_resize (lua_State *L, Table *t, unsigned int nasize,
                                          unsigned int nhsize) {
  unsigned int i;
  int j;
  unsigned int oldasize = t->sizearray;
  int oldhsize = allocsizenode(t);
  Node *nold = t->node;  /* save old hash ... */
  if (nasize > oldasize)  /* array part must grow? */
    setarrayvector(L, t, nasize);
  /* create new hash part with appropriate size */
  setnodevector(L, t, nhsize);
  if (nasize < oldasize) {  /* array part must shrink? */
    t->sizearray = nasize;
    /* re-insert elements from vanishing slice */
    for (i=nasize; i<oldasize; i++) {
      if (!ttisnil(&t->array[i]))
        luaH_setint(L, t, i + 1, &t->array[i]);
    }
    /* shrink array */
    luaM_reallocvector(L, t->array, oldasize, nasize, TValue);
  }
  /* re-insert elements from hash part */
  for (j = oldhsize - 1; j >= 0; j--) {
    Node *old = nold + j;
    if (!ttisnil(gval(old))) {
      /* doesn't need barrier/invalidate cache, as entry was
         already present in the table */
      setobjt2t(L, luaH_set(L, t, gkey(old)), gval(old));
    }
  }
  if (oldhsize > 0)  /* not the dummy node? */
    luaM_freearray(L, nold, cast(size_t, oldhsize)); /* free old hash */
}

// 重新调整数组部分大小
// allocsizenode返回以2为底的散列表大小的对数值
void luaH_resizearray (lua_State *L, Table *t, unsigned int nasize) {
  int nsize = allocsizenode(t);
  luaH_resize(L, t, nasize, nsize);
}

/*
** nums[i] = number of keys 'k' where 2^(i - 1) < k <= 2^i
*/
// 做重新哈希操作
// 首先分配一个位图nums，其中元素置0.这个位图的意义在于：nums数组中第i个元素存放的是key在2的i-1次幂和2的i次幂之间的元素数量。
// numusearray获取数组部分的元素数量，并且对nums数组的对应元素进行赋值
// numusehash遍历t中的哈希部分，因为其中可能也存在了正整数，需要根据这里的正整数数量更新对应的nums数组元素数量
// 然后计算额外新添加的key，并且更新nums数组和totaluse
// 此时，nums数组已经有了当前这个table中所有正整数的分配统计，然后调用computesizes计算哈希之后的数组部分的大小
static void rehash (lua_State *L, Table *t, const TValue *ek) {
  unsigned int asize;  /* optimal size for array part */
  unsigned int na;  /* number of keys in the array part */
  unsigned int nums[MAXABITS + 1];
  int i;
  int totaluse;
  for (i = 0; i <= MAXABITS; i++) nums[i] = 0;  /* reset counts */
  na = numusearray(t, nums);  /* count keys in array part */
  totaluse = na;  /* all those keys are integer keys */
  totaluse += numusehash(t, nums, &na);  /* count keys in hash part */
  /* count extra key */
  na += countint(ek, nums);
  totaluse++;
  /* compute new size for array part */
  asize = computesizes(nums, &na);
  /* resize the table to new computed sizes */
  luaH_resize(L, t, asize, totaluse - na);
}



/*
** }=============================================================
*/

// luaH_new创建的Table开始实际上是一张空表
// luaC_newobj作用是创建一个可被回收的对象（需要提供类型和大小），并且被链接到gc列表中
// gco2t宏的作用是把gc对象转换为table，内部操作为把o转化为GCUnion之后，再取h（代表table）的地址
// 调用setnodevector()为表哈希节点项分配内存并初始化
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

// free函数用作释放table的内存，原理很简单
// 释放一下array部分和node部分的内存和table本身的内存
// 释放node部分内存时候先要判断一下哈希部分有没有元素
// 下面三个函数的具体分析在lmem.h中
void luaH_free (lua_State *L, Table *t) {
  if (!isdummy(t))
    luaM_freearray(L, t->node, cast(size_t, sizenode(t)));
  luaM_freearray(L, t->array, t->sizearray);
  luaM_free(L, t);
}

// getfreepos的作用就是从哈希部分的后面开始找可以用的空闲节点,没有找到的话就返回NULL重新哈希
// 其实看到看到这里也明白了,lua里面的哈希填充是从后面开始的
static Node *getfreepos (Table *t) {
  if (!isdummy(t)) {
    while (t->lastfree > t->node) {
      t->lastfree--;
      if (ttisnil(gkey(t->lastfree)))
        return t->lastfree;
    }
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
// 在插入一个新的key的时候首先判断key是不是NULL，是的话就报错
// 然后接着判断，如果是数字，若是未定义数字也错误返回
// luaV_tointeger看一下 key的索引是否可以转换为int，此处0的意思就是不接受取整，只能是int
// 然后用构造出来的aux对key初始化，这样就算是处理完了key，在拿着key来取得最重要的mainposition
// 然后调用mainposition求出t中key对应的mainposition，返回值是一个Node *类型

// 如果t的哈希部分为NULL或者mp节点不为NULL都会走if的逻辑
// 首先就是定义一个othern的节点和一个f节点
// getfreepos获取一个可以用的空闲节点,如果返回NULL则说明哈希部分满员了 需要重新哈希
// 当返回一个可用的节点时，会判断:
// 1.如果属于同一主位置节点链下 (现有的 mp 位置的键的主位置节点和新插入的 key 的主位置节点确实指向的同一个节点)，
// 那么把空节点插入到主位置节点之后（每个节点的 TKey 结构中存储着指向下一个节点的偏移值）。走else逻辑.这个地方有点绕,好好理解
// 2) 如果不属于同一主位置节点链下，则意味着原本通过 mainposition(newkey) 直接定位的节点被其它节点链中的某个节点占用,走if逻辑
// 这里的othern保存的就是得到的mp上的node里面的key对应的mp值
// if逻辑：
// 如果得到的mp被其他节点链中的节点占用时，则由othern开始开始向后遍历一直到mp的前一个位置，然后将mp的数据移动到空节点上，othern指向新的节点，旧的节点用来放入新插入的键
// gnext(othern) = cast_int(f - othern);  让othern指向f节点
// else逻辑：
// 将f节点插入mp节点之后
// 然后就设置一下mp的k，并且返回mp的val，让用户操作
TValue *luaH_newkey (lua_State *L, Table *t, const TValue *key) {
  Node *mp;
  TValue aux;
  if (ttisnil(key)) luaG_runerror(L, "table index is nil");
  else if (ttisfloat(key)) {
    lua_Integer k;
    if (luaV_tointeger(key, &k, 0)) {  /* does index fit in an integer? */
      setivalue(&aux, k);
      key = &aux;  /* insert it as an integer */
    }
    else if (luai_numisnan(fltvalue(key)))
      luaG_runerror(L, "table index is NaN");
  }
  mp = mainposition(t, key);
  if (!ttisnil(gval(mp)) || isdummy(t)) {  /* main position is taken? */
    Node *othern;
    Node *f = getfreepos(t);  /* get a free place */
    if (f == NULL) {  /* cannot find a free place? */
      rehash(L, t, key);  /* grow table */
      /* whatever called 'newkey' takes care of TM cache */
      return luaH_set(L, t, key);  /* insert key into grown table */
    }
    lua_assert(!isdummy(t));
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
// 返回key对应的i字段
const TValue *luaH_getint (Table *t, lua_Integer key) {
  /* (1 <= key && key <= t->sizearray) */
  if (l_castS2U(key) - 1 < t->sizearray)
    return &t->array[key - 1];
  else {
    Node *n = hashint(t, key);
    for (;;) {  /* check whether 'key' is somewhere in the chain */
      if (ttisinteger(gkey(n)) && ivalue(gkey(n)) == key)
        return gval(n);  /* that's it */
      else {
        int nx = gnext(n);
        if (nx == 0) break;
        n += nx;
      }
    }
    return luaO_nilobject;
  }
}


/*
** search function for short strings
*/
// 返回key对应的tsv字段
const TValue *luaH_getshortstr (Table *t, TString *key) {
  Node *n = hashstr(t, key);
  lua_assert(key->tt == LUA_TSHRSTR);
  for (;;) {  /* check whether 'key' is somewhere in the chain */
    const TValue *k = gkey(n);
    if (ttisshrstring(k) && eqshrstr(tsvalue(k), key))
      return gval(n);  /* that's it */
    else {
      int nx = gnext(n);
      if (nx == 0)
        return luaO_nilobject;  /* not found */
      n += nx;
    }
  }
}


/*
** "Generic" get version. (Not that generic: not valid for integers,
** which may be in array part, nor for floats with integral values.)
*/
// 返回t中key对应的val值，else是在mp链表里面遍历
static const TValue *getgeneric (Table *t, const TValue *key) {
  Node *n = mainposition(t, key);
  for (;;) {  /* check whether 'key' is somewhere in the chain */
    if (luaV_rawequalobj(gkey(n), key))
      return gval(n);  /* that's it */
    else {
      int nx = gnext(n);
      if (nx == 0)
        return luaO_nilobject;  /* not found */
      n += nx;
    }
  }
}


const TValue *luaH_getstr (Table *t, TString *key) {
  if (key->tt == LUA_TSHRSTR)
    return luaH_getshortstr(t, key);
  else {  /* for long strings, use generic case */
    TValue ko;
    setsvalue(cast(lua_State *, NULL), &ko, key);
    return getgeneric(t, &ko);
  }
}


/*
** main search function
*/
// 获取key对应的具体的值
// short string就是tsv结构体
// int就是i字段
// NULL的话就是luaO_nilobject
// float的话就转化成int返回
// 缺省的话就返回key对应的val值
const TValue *luaH_get (Table *t, const TValue *key) {
  switch (ttype(key)) {
    case LUA_TSHRSTR: return luaH_getshortstr(t, tsvalue(key));
    case LUA_TNUMINT: return luaH_getint(t, ivalue(key));
    case LUA_TNIL: return luaO_nilobject;
    case LUA_TNUMFLT: {
      lua_Integer k;
      if (luaV_tointeger(key, &k, 0)) /* index is int? */
        return luaH_getint(t, k);  /* use specialized version */
      /* else... */
    }  /* FALLTHROUGH */
    default:
      return getgeneric(t, key);
  }
}


/*
** beware: when using this function you probably need to check a GC
** barrier and invalidate the TM cache.
*/
// 如果传进来的key已经存在于t中，那么直接返回key对应的索引，不然就新建一个key
TValue *luaH_set (lua_State *L, Table *t, const TValue *key) {
  const TValue *p = luaH_get(t, key);
  if (p != luaO_nilobject)
    return cast(TValue *, p);
  else return luaH_newkey(L, t, key);
}


// 把t中key的value设置为传进来的value
// 此处的关键在于cell，if里面的cell是当key存在于table里面对应的key，else里面是当key不存在于table时新建的key
// 然后调用setobj2t把cell的value和tt字段设为value相关的字段
void luaH_setint (lua_State *L, Table *t, lua_Integer key, TValue *value) {
  const TValue *p = luaH_getint(t, key);
  TValue *cell;
  if (p != luaO_nilobject)
    cell = cast(TValue *, p);
  else {
    TValue k;
    setivalue(&k, key);
    cell = luaH_newkey(L, t, &k);
  }
  setobj2t(L, cell, value);
}


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
      return i - 1;
    }
    j *= 2;
  }
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
// 获取t的size，这个接口在5.2后面被废除了
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
  else if (isdummy(t))  /* hash part is empty? */
    return j;  /* that is easy... */
  else return unbound_search(t, j);
}



#if defined(LUA_DEBUG)

// 返回t中key对应的mp
Node *luaH_mainposition (const Table *t, const TValue *key) {
  return mainposition(t, key);
}

// 检查t的哈希部分是否只有一个虚拟节点
int luaH_isdummy (const Table *t) { return isdummy(t); }

#endif
