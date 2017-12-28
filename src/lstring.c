/*
** $Id: lstring.c,v 2.56 2015/11/23 11:32:51 roberto Exp $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#define lstring_c
#define LUA_CORE

#include "lprefix.h"


#include <string.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"


#define MEMERRMSG       "not enough memory"


/*
** Lua will use at most ~(2^LUAI_HASHLIMIT) bytes from a string to
** compute its hash
*/
#if !defined(LUAI_HASHLIMIT)
#define LUAI_HASHLIMIT		5
#endif


/*
** equality for long strings
*/
// 比较长字符串a和b是否相等
// 首先检查a和b的类型是否相等
// 然后检查a和b是否指向同一对象
// 然后比较长度是否相等，最后调用memcmp来比较内存内容
// memcmp是比较内存区域buf1和buf2的前count个字节。该函数是按字节比较的。
// int memcmp(const void *buf1, const void *buf2, unsigned int count);
// 当buf1<buf2时，返回值小于0
// 当buf1==buf2时，返回值=0
// 当buf1>buf2时，返回值大于0
int luaS_eqlngstr (TString *a, TString *b) {
  size_t len = a->u.lnglen;
  lua_assert(a->tt == LUA_TLNGSTR && b->tt == LUA_TLNGSTR);
  return (a == b) ||  /* same instance or... */
    ((len == b->u.lnglen) &&  /* equal length and ... */
     (memcmp(getstr(a), getstr(b), len) == 0));  /* equal contents */
}

// 计算字符串的哈希值
// 对于长度小于2^LUAI_HASHLIMIT的字符串，每一个字节都会参加hash计算，长度大于等于32的字符串，从末尾开始，每(1 >> LUAI_HASHLIMIT) + 1位参与hash计算
// str是待计算hash的字符串，s是字符串的长度，seed是哈希算法随机种子
// 此处的随机种子是在创建虚拟机的global_State(全局状态机)时构造并存储在global_State中的
// 此处需要注意一下setp的计算
// 首先把把l向右移动5位，然后加1，这样就使长度小于32的字符串的步长变为了1
unsigned int luaS_hash (const char *str, size_t l, unsigned int seed) {
  unsigned int h = seed ^ cast(unsigned int, l);
  size_t step = (l >> LUAI_HASHLIMIT) + 1;
  for (; l >= step; l -= step)
    h ^= ((h<<5) + (h>>2) + cast_byte(str[l - 1]));
  return h;
}


// 计算长字符串的哈希值
// 首先判断是不是长字符串,然后判断是否hash过(这里是根据extra字段,extra为1的话,不需要再进行hash操作,短字符串默认就是1)
// 然后再调用上面计算哈希值的函数就可以了,这里传递的hash字段是字符串在创建的时候从global_State拿到的哈希随机种子
// 然后extra置为1
unsigned int luaS_hashlongstr (TString *ts) {
  lua_assert(ts->tt == LUA_TLNGSTR);
  if (ts->extra == 0) {  /* no hash? */
    ts->hash = luaS_hash(getstr(ts), ts->u.lnglen, ts->hash);
    ts->extra = 1;  /* now it has its hash */
  }
  return ts->hash;
}


/*
** resizes the string table
*/
void luaS_resize (lua_State *L, int newsize) {
  int i;
  stringtable *tb = &G(L)->strt;
  if (newsize > tb->size) {  /* grow table if needed */
    luaM_reallocvector(L, tb->hash, tb->size, newsize, TString *);
    for (i = tb->size; i < newsize; i++)
      tb->hash[i] = NULL;
  }
  for (i = 0; i < tb->size; i++) {  /* rehash */
    TString *p = tb->hash[i];
    tb->hash[i] = NULL;
    while (p) {  /* for each node in the list */
      TString *hnext = p->u.hnext;  /* save next */
      unsigned int h = lmod(p->hash, newsize);  /* new position */
      p->u.hnext = tb->hash[h];  /* chain it */
      tb->hash[h] = p;
      p = hnext;
    }
  }
  if (newsize < tb->size) {  /* shrink table if needed */
    /* vanishing slice should be empty */
    lua_assert(tb->hash[newsize] == NULL && tb->hash[tb->size - 1] == NULL);
    luaM_reallocvector(L, tb->hash, tb->size, newsize, TString *);
  }
  tb->size = newsize;
}


/*
** Clear API string cache. (Entries cannot be empty, so fill them with
** a non-collectable string.)
*/
void luaS_clearcache (global_State *g) {
  int i, j;
  for (i = 0; i < STRCACHE_N; i++)
    for (j = 0; j < STRCACHE_M; j++) {
    if (iswhite(g->strcache[i][j]))  /* will entry be collected? */
      g->strcache[i][j] = g->memerrmsg;  /* replace it with something fixed */
    }
}


/*
** Initialize the string table and the string cache
*/
void luaS_init (lua_State *L) {
  global_State *g = G(L);
  int i, j;
  luaS_resize(L, MINSTRTABSIZE);  /* initial size of string table */
  /* pre-create memory-error message */
  g->memerrmsg = luaS_newliteral(L, MEMERRMSG);
  luaC_fix(L, obj2gco(g->memerrmsg));  /* it should never be collected */
  for (i = 0; i < STRCACHE_N; i++)  /* fill cache with valid strings */
    for (j = 0; j < STRCACHE_M; j++)
      g->strcache[i][j] = g->memerrmsg;
}



/*
** creates a new string object
*/
// l字符串的长度，tag是字符串的类型，h是默认的hash种子
// sizelstring就是求出UTString的size
// luaC_newobj创建一个可以被GC的对象
// 然后再把o转换成TString类型，继续设置ts的hash字段，和extra字段(extra用于标记是否是虚拟机保留的字符串，如果这个值为1，那么不会GC)
// 然后把字符串的最后以'\0'结尾
static TString *createstrobj (lua_State *L, size_t l, int tag, unsigned int h) {
  TString *ts;
  GCObject *o;
  size_t totalsize;  /* total size of TString object */
  totalsize = sizelstring(l);
  o = luaC_newobj(L, tag, totalsize);
  ts = gco2ts(o);
  ts->hash = h;
  ts->extra = 0;
  getstr(ts)[l] = '\0';  /* ending 0 */
  return ts;
}


// 创建一个长字符串
// 调用createstrobj
TString *luaS_createlngstrobj (lua_State *L, size_t l) {
  TString *ts = createstrobj(L, l, LUA_TLNGSTR, G(L)->seed);
  ts->u.lnglen = l;
  return ts;
}

// 从全局变量就是global_State的strt成员里面移除特定字符串
// 首先得到tb，指向strt数组，然后再通过tb的hash数组通过提供tb的长度和字符串的hash，来找到字符串属于哪个链表
// 然后一直循环，直到找到等于ts的，然后就把这个字符串的地址给抹去了(不会内存泄漏？？？)
void luaS_remove (lua_State *L, TString *ts) {
  stringtable *tb = &G(L)->strt;
  TString **p = &tb->hash[lmod(ts->hash, tb->size)];
  while (*p != ts)  /* find previous element */
    p = &(*p)->u.hnext;
  *p = (*p)->u.hnext;  /* remove element from its list */
  tb->nuse--;
}


/*
** checks whether short string exists and reuses it or creates a new one
*/
// 判断这个字符串是否存在，存在的话就重用不然就创建一个新的
static TString *internshrstr (lua_State *L, const char *str, size_t l) {
  TString *ts;
  global_State *g = G(L);
  unsigned int h = luaS_hash(str, l, g->seed);
  TString **list = &g->strt.hash[lmod(h, g->strt.size)];
  lua_assert(str != NULL);  /* otherwise 'memcmp'/'memcpy' are undefined */
  for (ts = *list; ts != NULL; ts = ts->u.hnext) {
    if (l == ts->shrlen &&
        (memcmp(str, getstr(ts), l * sizeof(char)) == 0)) {
      /* found! */
      if (isdead(g, ts))  /* dead (but not collected yet)? */
        changewhite(ts);  /* resurrect it */
      return ts;
    }
  }
  if (g->strt.nuse >= g->strt.size && g->strt.size <= MAX_INT/2) {
    luaS_resize(L, g->strt.size * 2);
    list = &g->strt.hash[lmod(h, g->strt.size)];  /* recompute with new size */
  }
  ts = createstrobj(L, l, LUA_TSHRSTR, h);
  memcpy(getstr(ts), str, l * sizeof(char));
  ts->shrlen = cast_byte(l);
  ts->u.hnext = *list;
  *list = ts;
  g->strt.nuse++;
  return ts;
}


/*
** new string (with explicit length)
*/
// LUAI_MAXSHORTLEN是40，定义在llimits.h中，用作判断字符串是长字符串还是短字符串
// 在这里，如果是短字符串的话，就调用internshrstr
// 如果是长字符串的话，就先新建一个字符串变量(TString)
// 如果长度太大的话就报错，如何判断长度太大，首先你要明白MAX_SIZE是啥,有关MAX_SIZE的定义在llimits.h中
TString *luaS_newlstr (lua_State *L, const char *str, size_t l) {
  if (l <= LUAI_MAXSHORTLEN)  /* short string? */
    return internshrstr(L, str, l);
  else {
    TString *ts;
    if (l >= (MAX_SIZE - sizeof(TString))/sizeof(char))
      luaM_toobig(L);
    ts = luaS_createlngstrobj(L, l);
    memcpy(getstr(ts), str, l * sizeof(char));
    return ts;
  }
}


/*
** Create or reuse a zero-terminated string, first checking in the
** cache (using the string address as a key). The cache can contain
** only zero-terminated strings, so it is safe to use 'strcmp' to
** check hits.
*/
// 创建或重用一个以'\0'结尾的字符串，首先检查缓存(使用字符串的地址做为key来获取字符串)
// 这个缓存能够包含以'\0'结尾的字符串，因此使用'strcmp'来检查是否命中是安全的

// 在lua中，字符串是被内化的一种数据结构，内化的意思就是说，每个存放lua字符串的变量，实际上存放的并不是一份字符串的数据副本，而是这份字符串的引用
// 因此，在lua中字符串是一个不可变的数据，然后呐，为了实现内化，在lua虚拟机中必然要存在一个全局的地方存放当前系统中的所有字符串，lua虚拟机使用一个散列通来管理字符串
// 上面说到了lua虚拟机中存在一个全局的地方存放字符串，这个全局变量就是global_State的strt成员。它是一个散列数组
// 当创建一个字符串时，首先根据哈希算法，算出哈希值，这个算出来的哈希值就是strt数组的索引值，如果这个地方已经有值了，则使用链表串接起来（串接部分不能超过STRCACHE_M）
// p指向的是strt的第一个链表，然后再比较str和当前的字符串有没有一样的，有的话，直接返回
// 如果不存在一样的字符串的话，就创建一个
// 首先要做的操作就是把当前strt中的元素，全部往后移一个位置，p[0]用来存放新建的字符串
TString *luaS_new (lua_State *L, const char *str) {
  unsigned int i = point2uint(str) % STRCACHE_N;  /* hash */
  int j;
  TString **p = G(L)->strcache[i];
  for (j = 0; j < STRCACHE_M; j++) {
    if (strcmp(str, getstr(p[j])) == 0)  /* hit? */
      return p[j];  /* that is it */
  }
  /* normal route */
  for (j = STRCACHE_M - 1; j > 0; j--)
    p[j] = p[j - 1];  /* move out last element */
  /* new element is first in the list */
  p[0] = luaS_newlstr(L, str, strlen(str));
  return p[0];
}


Udata *luaS_newudata (lua_State *L, size_t s) {
  Udata *u;
  GCObject *o;
  if (s > MAX_SIZE - sizeof(Udata))
    luaM_toobig(L);
  o = luaC_newobj(L, LUA_TUSERDATA, sizeludata(s));
  u = gco2u(o);
  u->len = s;
  u->metatable = NULL;
  setuservalue(L, u, luaO_nilobject);
  return u;
}

