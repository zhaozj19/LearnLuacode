/*
** $Id: lbaselib.c,v 1.314 2016/09/05 19:06:34 roberto Exp $
** Basic library
** See Copyright Notice in lua.h
*/

#define lbaselib_c
#define LUA_LIB

#include "lprefix.h"


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"


// 接收任意数量的参数，并将它们的值打印到 stdout。 它用 tostring 函数将每个参数都转换为字符串。 
// print 不用于做格式化输出。仅作为看一下某个值的快捷方式。 多用于调试。 

// 首先 通过lua_gettop获取参数的数量n,
// lua_getglobal是把全局变量 name 里的值压栈，返回该值的类型。
// lua_pushvalue是把栈上给定索引处的元素作一个副本压栈。
// lua_call调用一个函数（首先需要把调用的函数压入栈，接着把需要传递给这个函数的参数按正确的顺序入栈。
// 函数调用完毕后，所有的参数以及函数本身都会出栈。而函数的返回值这时入栈）
// lua_tolstring把给定索引处的 Lua 值转换为一个 C 字符串. l用来保存长度
// lua_writestring定义在lauxlib.h中，是C函数fwrite的宏
// fwrite是向指定的文件中写入若干数据块，如成功执行则返回实际写入的数据块数目。该函数以二进制形式对文件进行操作，不局限于文本文件。
// s：是一个指针，用于获取要存入的数据
// size：要写入内容的单字节数
// l：要写入数据的长度
// stdout：目标文件指针（这里是标准输出文件）
// #define lua_writestring(s,l)   fwrite((s), sizeof(char), (l), stdout)
// lua_writeline是lua_writestring的宏，作用是换行
// #define lua_writeline()        (lua_writestring("\n", 1), fflush(stdout))
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_print (lua_State *L) {
  int n = lua_gettop(L);  /* number of arguments */
  int i;
  lua_getglobal(L, "tostring");
  for (i=1; i<=n; i++) {
    const char *s;
    size_t l;
    lua_pushvalue(L, -1);  /* function to be called */
    lua_pushvalue(L, i);   /* value to print */
    lua_call(L, 1, 1);
    s = lua_tolstring(L, -1, &l);  /* get result */
    if (s == NULL)
      return luaL_error(L, "'tostring' must return a string to 'print'");
    if (i>1) lua_writestring("\t", 1);
    lua_writestring(s, l);
    lua_pop(L, 1);  /* pop result */
  }
  lua_writeline();
  return 0;
}


#define SPACECHARS  " \f\n\r\t\v"


// lua_Unsigned无符号int64
// strspn返回字符串s开头连续包含字符串accept内的字符数目
// neg是数字的符号
// 如果这个字符不是字母或者数字则返回（isalnum）
// 如果这个字符是数字（isdigit）则通过减去'0'得到数字
// 是字母的话通过减去'A'+10得到数字，然后看这个数字是不是超出了base的限制
// -------------------------------------------------------------------------------------------------------------------------------------------
static const char *b_str2int (const char *s, int base, lua_Integer *pn) {
  lua_Unsigned n = 0;
  int neg = 0;
  s += strspn(s, SPACECHARS);  /* skip initial spaces */
  if (*s == '-') { s++; neg = 1; }  /* handle signal */
  else if (*s == '+') s++;
  if (!isalnum((unsigned char)*s))  /* no digit? */
    return NULL;
  do {
    int digit = (isdigit((unsigned char)*s)) ? *s - '0'
                   : (toupper((unsigned char)*s) - 'A') + 10;
    if (digit >= base) return NULL;  /* invalid numeral */
    n = n * base + digit;
    s++;
  } while (isalnum((unsigned char)*s));
  s += strspn(s, SPACECHARS);  /* skip trailing spaces */
  *pn = (lua_Integer)((neg) ? (0u - n) : n);
  return s;
}

// 把给定索引处的 Lua 值转换为 lua_Number 这样一个 C 类型
// 这个 Lua 值必须是一个数字或是一个可转换为数字的字符串 否则，lua_tonumber返回 0 。
// 这里的tonumber使用有两种方法
// 1：tonumber("100")  --100 
// 2：tonumber("100",2)   --4
// 而且第二个参数的范围是2 -- 36
// 比如说tonumber("z",36)   --结果是35
// 这里的z通过大写之后减去'A'然后加10，得到35，也就是说对于tonumber来说a,b,c...就是10,11,12
// 而后面这个base是一个限制，我这里写36意思就是最大可以支持到z（36-10）
// 写2的话,就是说前面的字符，最小也要大于2，，否则就返回nil

// lua_isnoneornil:当给定索引无效或其值是 nil 时， 返回 1 ，否则返回 0 。
// 这里如果只有一个参数就普通的转换
// luaL_checkany检查函数在 arg 位置是否有任何类型（包括 nil）的参数。
// 如果本身已经是一个number，那就不用转换。
// 如果不是的话，看看是否这个s可以转换为数字，可以的话，转换完入栈，不然抛出错误
// 第二种情况是字符串的情况，s用来保存传进来的字符串
// 然后检查base的合法性（为什么是2和36）再利用b_str2int基础函数进行转换，结果保存在n中，然后 入栈。
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_tonumber (lua_State *L) {
  if (lua_isnoneornil(L, 2)) {  /* standard conversion? */
    luaL_checkany(L, 1);
    if (lua_type(L, 1) == LUA_TNUMBER) {  /* already a number? */
      lua_settop(L, 1);  /* yes; return it */
      return 1;
    }
    else {
      size_t l;
      const char *s = lua_tolstring(L, 1, &l);
      if (s != NULL && lua_stringtonumber(L, s) == l + 1)
        return 1;  /* successful conversion to number */
      /* else not a number */
    }
  }
  else {
    size_t l;
    const char *s;
    lua_Integer n = 0;  /* to avoid warnings */
    lua_Integer base = luaL_checkinteger(L, 2);
    luaL_checktype(L, 1, LUA_TSTRING);  /* no numbers as strings */
    s = lua_tolstring(L, 1, &l);
    luaL_argcheck(L, 2 <= base && base <= 36, 2, "base out of range");
    if (b_str2int(s, (int)base, &n) == s + l) {
      lua_pushinteger(L, n);
      return 1;
    }  /* else not a number */
  }  /* else not a number */
  lua_pushnil(L);  /* not a number */
  return 1;
}

// luaL_optinteger：如果函数的第 2 个参数是一个 整数（或可以转换为一个整数）， 返回该整数。若该参数不存在或是 nil， 返回 第三个参数
// luaL_where：将一个用于表示 level 层栈的控制点位置的字符串压栈。
// lua_concat：连接栈顶的 n 个值， 然后将这些值出栈，并把结果放在栈顶。
// lua_error以栈顶的值作为错误对象，抛出一个 Lua 错误。 这个函数将做一次长跳转，所以一定不会返回
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_error (lua_State *L) {
  int level = (int)luaL_optinteger(L, 2, 1);
  lua_settop(L, 1);
  if (lua_type(L, 1) == LUA_TSTRING && level > 0) {
    luaL_where(L, level);   /* add extra information */
    lua_pushvalue(L, 1);
    lua_concat(L, 2);
  }
  return lua_error(L);
}

// lua_getmetatable：如果该索引处的值有元表，则将其元表压栈，返回 1 。 否则不会将任何东西入栈，返回 0 。
// luaL_getmetafield：将索引 obj 处对象的元表中 __metatable 域的值压栈。
// 如果该对象没有元表，或是该元表没有相关域， 此函数什么也不会压栈并返回 LUA_TNIL。
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_getmetatable (lua_State *L) {
  luaL_checkany(L, 1);
  if (!lua_getmetatable(L, 1)) {
    lua_pushnil(L);
    return 1;  /* no metatable */
  }
  luaL_getmetafield(L, 1, "__metatable");
  return 1;  /* returns either __metatable field (if present) or metatable */
}

// LUA_TNIL定义在lua.h中 是0
// lua_setmetatable：把一张表弹出栈，并将其设为给定索引处的值的元表。
// a = {}
// setmetatable(a,{__metatable = "hello"})
// setmetatable(a,{__metatable = "world"})
// 这种情况就会报错
// 也就是说 __metatable这个是保护元表,不可读写
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_setmetatable (lua_State *L) {
  int t = lua_type(L, 2);
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_argcheck(L, t == LUA_TNIL || t == LUA_TTABLE, 2,
                    "nil or table expected");
  if (luaL_getmetafield(L, 1, "__metatable") != LUA_TNIL)
    return luaL_error(L, "cannot change a protected metatable");
  lua_settop(L, 2);
  lua_setmetatable(L, 1);
  return 1;
}

// 在不触发任何元方法的情况下 检查 v1 是否和 v2 相等。 返回一个布尔量。

// lua_rawequal：如果索引 index1 与索引 index2 处的值 本身相等（即不调用元方法），返回 1 。 否则返回 0 。 当任何一个索引无效时，也返回 0 。
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_rawequal (lua_State *L) {
  luaL_checkany(L, 1);
  luaL_checkany(L, 2);
  lua_pushboolean(L, lua_rawequal(L, 1, 2));
  return 1;
}


// 在不触发任何元方法的情况下 返回对象 v 的长度。 v 可以是表或字符串。 它返回一个整数。

// lua_rawlen：
// 返回给定索引处值的固有“长度”： 
// 对于字符串，它指字符串的长度； 
// 对于表；它指不触发元方法的情况下取长度操作（'#'）应得到的值； 
// 对于用户数据，它指为该用户数据分配的内存块的大小； 对于其它值，它为 0 。
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_rawlen (lua_State *L) {
  int t = lua_type(L, 1);
  luaL_argcheck(L, t == LUA_TTABLE || t == LUA_TSTRING, 1,
                   "table or string expected");
  lua_pushinteger(L, lua_rawlen(L, 1));
  return 1;
}

// 在不触发任何元方法的情况下 获取 table[index] 的值。 table 必须是一张表； index 可以是任何值。

// lua_rawget：类似于 lua_gettable ， 但是作一次直接访问（不触发元方法）。
// void lua_gettable (lua_State *L, int index);
// 把 t[k] 值压入堆栈， 这里的 t 是指有效索引 index 指向的值， 而 k 则是栈顶放的值。
// 这个函数会弹出堆栈上的 key （把结果放在栈上相同位置）。 在 Lua 中，这个函数可能触发对应 "index" 事件的元方法 
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_rawget (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checkany(L, 2);
  lua_settop(L, 2);
  lua_rawget(L, 1);
  return 1;
}

// 在不触发任何元方法的情况下 将 table[index] 设为 value。 table 必须是一张表， index 可以是 nil 与 NaN 之外的任何值。 value 可以是任何 Lua 值。

// lua_rawset：类似于 lua_settable ， 但是是做一次直接赋值（不触发元方法）。
// void lua_settable (lua_State *L, int index);
// 作一个等价于 t[k] = v 的操作， 这里 t 是一个给定有效索引 index 处的值， v 指栈顶的值， 而 k 是栈顶之下的那个值。
// 这个函数会把键和值都从堆栈中弹出。 和在 Lua 中一样，这个函数可能触发 "newindex" 事件的元方法 
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_rawset (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checkany(L, 2);
  luaL_checkany(L, 3);
  lua_settop(L, 3);
  lua_rawset(L, 1);
  return 1;
}

// collectgarbage ([opt [, arg]])
// 这个函数是垃圾收集器的通用接口。 通过参数 opt 它提供了一组不同的功能：
// "collect": 做一次完整的垃圾收集循环。 这是默认选项。
// "stop": 停止垃圾收集器的运行。 在调用重启前，收集器只会因显式的调用运行。
// "restart": 重启垃圾收集器的自动运行。
// "count": 以 K 字节数为单位返回 Lua 使用的总内存数。 这个值有小数部分，所以只需要乘上 1024 就能得到 Lua 使用的准确字节数（除非溢出）。
// "step": 单步运行垃圾收集器。 步长“大小”由 arg 控制。 传入 0 时，收集器步进（不可分割的）一步。 传入非 0 值， 收集器收集相当于 Lua 分配这些多（K 字节）内存的工作。 如果收集器结束一个循环将返回 true 。
// "setpause": 将 arg 设为收集器的 间歇率 。 返回 间歇率 的前一个值。
// "setstepmul": 将 arg 设为收集器的 步进倍率 。 返回 步进倍率 的前一个值。
// "isrunning": 返回表示收集器是否在工作的布尔值 （即未被停止）。
// 在lua.h中有如下定义：
// #define LUA_GCSTOP              0
// #define LUA_GCRESTART           1
// #define LUA_GCCOLLECT           2
// #define LUA_GCCOUNT             3
// #define LUA_GCCOUNTB            4
// #define LUA_GCSTEP              5
// #define LUA_GCSETPAUSE          6
// #define LUA_GCSETSTEPMUL        7
// #define LUA_GCISRUNNING         9
// luaL_checkoption：检查函数的第 arg 个参数是否是一个 字符串，并在数组 lst （比如是零结尾的字符串数组） 中查找这个字符串。 返回匹配到的字符串在数组中的索引号。 
//                   如果参数不是字符串，或是字符串在数组中匹配不到，都将抛出错误。
// 所以这里o保存了optsnum的元素，然后通过switch
// int lua_gc (lua_State *L, int what, int data);
// 控制垃圾收集器。
// 这个函数根据其参数 what 发起几种不同的任务
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_collectgarbage (lua_State *L) {
  static const char *const opts[] = {"stop", "restart", "collect",
    "count", "step", "setpause", "setstepmul",
    "isrunning", NULL};
  static const int optsnum[] = {LUA_GCSTOP, LUA_GCRESTART, LUA_GCCOLLECT,
    LUA_GCCOUNT, LUA_GCSTEP, LUA_GCSETPAUSE, LUA_GCSETSTEPMUL,
    LUA_GCISRUNNING};
  int o = optsnum[luaL_checkoption(L, 1, "collect", opts)];
  int ex = (int)luaL_optinteger(L, 2, 0);
  int res = lua_gc(L, o, ex);
  switch (o) {
    case LUA_GCCOUNT: {
      int b = lua_gc(L, LUA_GCCOUNTB, 0);
      lua_pushnumber(L, (lua_Number)res + ((lua_Number)b/1024));
      return 1;
    }
    case LUA_GCSTEP: case LUA_GCISRUNNING: {
      lua_pushboolean(L, res);
      return 1;
    }
    default: {
      lua_pushinteger(L, res);
      return 1;
    }
  }
}


// type返回参数的类型

// 返回给定有效索引处值的类型， 当索引无效（或无法访问）时则返回 LUA_TNONE。 
// lua_type 返回的类型被编码为一些个在 lua.h 中定义的常量： 
// LUA_TNIL， LUA_TNUMBER， LUA_TBOOLEAN， LUA_TSTRING， LUA_TTABLE， LUA_TFUNCTION， LUA_TUSERDATA， LUA_TTHREAD， LUA_TLIGHTUSERDATA。
// lua_typename：返回 t 表示的类型名， 这个 t 必须是 lua_type 可能返回的值中之一
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_type (lua_State *L) {
  int t = lua_type(L, 1);
  luaL_argcheck(L, t != LUA_TNONE, 1, "value expected");
  lua_pushstring(L, lua_typename(L, t));
  return 1;
}

// luaL_getmetafield：将索引处1的元表中method域的部分入栈，如果没有元表或是没有相关域，返回LUA_TNIL
// lua_pushvalue：把栈上指定索引的副本入栈

// 这里首先判断lua栈里面的索引为1处的table有没有method相关域，没有的话做一系列操作然后返回next table 和nil
// 如果之前有method相关域的话，以 t 为参数调用它，并返回其返回的前三个值。
// -------------------------------------------------------------------------------------------------------------------------------------------
static int pairsmeta (lua_State *L, const char *method, int iszero,
                      lua_CFunction iter) {
  luaL_checkany(L, 1);
  if (luaL_getmetafield(L, 1, method) == LUA_TNIL) {  /* no metamethod? */
    lua_pushcfunction(L, iter);  /* will return generator, */
    lua_pushvalue(L, 1);  /* state, */
    if (iszero) lua_pushinteger(L, 0);  /* and initial value */
    else lua_pushnil(L);
  }
  else {
    lua_pushvalue(L, 1);  /* argument 'self' to metamethod */
    lua_call(L, 1, 3);  /* get 3 values from metamethod */
  }
  return 3;
}

// 运行程序来遍历表中的所有域。 第一个参数是要遍历的表，第二个参数是表中的某个键。 
// next 返回该键的下一个键及其关联的值。 如果用 nil 作为第二个参数调用 next 将返回初始键及其关联值。 
// 当以最后一个键去调用，或是以 nil 调用一张空表时， next 返回 nil。 
// 如果不提供第二个参数，将认为它就是 nil。 特别指出，你可以用 next(t) 来判断一张表是否是空的。
// 比如：print(next({1,2,3}))     -- 1    1
//       print(next({1,2,3},1))   -- 2    2
//       print(next({1,2,3},2))   -- 3    3
//       print(next({1,2,3},3))   -- nil
// lua_next：从栈顶弹出一个键， 然后把索引指定的表中的一个键值对压栈 （弹出的键之后的 “下一” 对）。 
// 如果表中以无更多元素， 那么 lua_next 将返回 0 （什么也不压栈）。
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_next (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_settop(L, 2);  /* create a 2nd argument if there isn't one */
  if (lua_next(L, 1))
    return 2;
  else {
    lua_pushnil(L);
    return 1;
  }
}



// 如果 t 有元方法 __pairs， 以 t 为参数调用它，并返回其返回的前三个值。
// 否则，返回三个值：next 函数， 表 t，以及 nil。 因此以下代码
//      for k,v in pairs(t) do body end
// 能迭代表 t 中的所有键值对。
// 当在遍历过程中你给表中并不存在的域赋值，next 的行为是未定义的。然而你可以去修改那些已存在的域。特别指出，你可以清除一些已存在的域。
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_pairs (lua_State *L) {
  return pairsmeta(L, "__pairs", 0, luaB_next);
}


/*
** Traversal function for 'ipairs'
*/
static int ipairsaux (lua_State *L) {
  lua_Integer i = luaL_checkinteger(L, 2) + 1;
  lua_pushinteger(L, i);
  return (lua_geti(L, 1, i) == LUA_TNIL) ? 1 : 2;
}


/*
** 'ipairs' function. Returns 'ipairsaux', given "table", 0.
** (The given "table" may not be a table.)
*/
// 返回三个值（迭代函数、表 t 以及 0 ）
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_ipairs (lua_State *L) {
#if defined(LUA_COMPAT_IPAIRS)
  return pairsmeta(L, "__ipairs", 1, ipairsaux);
#else
  luaL_checkany(L, 1);
  lua_pushcfunction(L, ipairsaux);  /* iteration function */
  lua_pushvalue(L, 1);  /* state */
  lua_pushinteger(L, 0);  /* initial value */
  return 3;
#endif
}


static int load_aux (lua_State *L, int status, int envidx) {
  if (status == LUA_OK) {
    if (envidx != 0) {  /* 'env' parameter? */
      lua_pushvalue(L, envidx);  /* environment for loaded function */
      if (!lua_setupvalue(L, -2, 1))  /* set it as 1st upvalue */
        lua_pop(L, 1);  /* remove 'env' if not used by previous call */
    }
    return 1;
  }
  else {  /* error (message is on top of the stack) */
    lua_pushnil(L);
    lua_insert(L, -2);  /* put before error message */
    return 2;  /* return nil plus error message */
  }
}


// loadfile ([filename [, mode [, env]]])  --后两个是可选参数
// 从文件 filename 或标准输入（如果文件名未提供）中获取代码块
// luaL_loadfilex：把一个文件加载为 Lua 代码块。
//                 这个函数使用 lua_load 加载文件中的数据。 代码块的名字被命名为 fname。
//                 返回值可以是：
//                 LUA_OK: 没有错误；
//                 LUA_ERRSYNTAX: 在预编译时碰到语法错误；
//                 LUA_ERRMEM: 内存分配错误；
//                 LUA_ERRGCMM: 在运行 __gc 元方法时出错了。
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_loadfile (lua_State *L) {
  const char *fname = luaL_optstring(L, 1, NULL);
  const char *mode = luaL_optstring(L, 2, NULL);
  int env = (!lua_isnone(L, 3) ? 3 : 0);  /* 'env' index or 0 if no 'env' */
  int status = luaL_loadfilex(L, fname, mode);
  return load_aux(L, status, env);
}


/*
** {======================================================
** Generic Read function
** =======================================================
*/


/*
** reserved slot, above all arguments, to hold a copy of the returned
** string to avoid it being collected while parsed. 'load' has four
** optional arguments (chunk, source name, mode, and environment).
*/
#define RESERVEDSLOT  5


/*
** Reader for generic 'load' function: 'lua_load' uses the
** stack for internal stuff, so the reader cannot change the
** stack top. Instead, it keeps its resulting string in a
** reserved slot inside the stack.
*/
static const char *generic_reader (lua_State *L, void *ud, size_t *size) {
  (void)(ud);  /* not used */
  luaL_checkstack(L, 2, "too many nested functions");
  lua_pushvalue(L, 1);  /* get function */
  lua_call(L, 0, 1);  /* call it */
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);  /* pop result */
    *size = 0;
    return NULL;
  }
  else if (!lua_isstring(L, -1))
    luaL_error(L, "reader function must return a string");
  lua_replace(L, RESERVEDSLOT);  /* save string in reserved slot */
  return lua_tolstring(L, RESERVEDSLOT, size);
}

// load (chunk [, chunkname [, mode [, env]]])
// 字符串 mode 用于控制代码块是文本还是二进制（即预编译代码块）。 
// 它可以是字符串 "b" （只能是二进制代码块）， "t" （只能是文本代码块）， 或 "bt" （可以是二进制也可以是文本）。 默认值为 "bt"。
// chunkname 在错误消息和调试消息中，用于代码块的名字。 
// 如果不提供此参数，它默认为字符串chunk 。 chunk 不是字符串时，则为 "=(load)" 。

// status用来保存加载lua代码块的结果状态，正常是LUA_OK
// l保存代码块长度
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_load (lua_State *L) {
  int status;
  size_t l;
  const char *s = lua_tolstring(L, 1, &l);
  const char *mode = luaL_optstring(L, 3, "bt");
  int env = (!lua_isnone(L, 4) ? 4 : 0);  /* 'env' index or 0 if no 'env' */
  if (s != NULL) {  /* loading a string? */
    const char *chunkname = luaL_optstring(L, 2, s);
    status = luaL_loadbufferx(L, s, l, chunkname, mode);
  }
  else {  /* loading from a reader function */
    const char *chunkname = luaL_optstring(L, 2, "=(load)");
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_settop(L, RESERVEDSLOT);  /* create reserved slot */
    status = lua_load(L, generic_reader, NULL, chunkname, mode);
  }
  return load_aux(L, status, env);
}

/* }====================================================== */


static int dofilecont (lua_State *L, int d1, lua_KContext d2) {
  (void)d1;  (void)d2;  /* only to match 'lua_Kfunction' prototype */
  return lua_gettop(L) - 1;
}

// 打开该名字的文件，并执行文件中的 Lua 代码块。 
// dofile 没有运行在保护模式下
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_dofile (lua_State *L) {
  const char *fname = luaL_optstring(L, 1, NULL);
  lua_settop(L, 1);
  if (luaL_loadfile(L, fname) != LUA_OK)
    return lua_error(L);
  lua_callk(L, 0, LUA_MULTRET, 0, dofilecont);
  return dofilecont(L, 0, 0);
}

// assert (v [, message])
// 如果其参数 v 的值为假（nil 或 false）， 它就调用 error；
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_assert (lua_State *L) {
  if (lua_toboolean(L, 1))  /* condition is true? */
    return lua_gettop(L);  /* return all arguments */
  else {  /* error */
    luaL_checkany(L, 1);  /* there must be a condition */
    lua_remove(L, 1);  /* remove it */
    lua_pushliteral(L, "assertion failed!");  /* default message */
    lua_settop(L, 1);  /* leave only message (default if no other one) */
    return luaB_error(L);  /* call 'error' */
  }
}


// select (index, ···)
// print(select('#',100,1))    --2
// 如果 index 是个数字， 那么返回参数中第 index 个之后的部分； 
// 负的数字会从后向前索引（-1 指最后一个参数）。 否则，index 必须是字符串 "#"， 此时 select 返回参数的个数。
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_select (lua_State *L) {
  int n = lua_gettop(L);
  if (lua_type(L, 1) == LUA_TSTRING && *lua_tostring(L, 1) == '#') {
    lua_pushinteger(L, n-1);
    return 1;
  }
  else {
    lua_Integer i = luaL_checkinteger(L, 1);
    if (i < 0) i = n + i;
    else if (i > n) i = n;
    luaL_argcheck(L, 1 <= i, 1, "index out of range");
    return n - (int)i;
  }
}


/*
** Continuation function for 'pcall' and 'xpcall'. Both functions
** already pushed a 'true' before doing the call, so in case of success
** 'finishpcall' only has to return everything in the stack minus
** 'extra' values (where 'extra' is exactly the number of items to be
** ignored).
*/
static int finishpcall (lua_State *L, int status, lua_KContext extra) {
  if (status != LUA_OK && status != LUA_YIELD) {  /* error? */
    lua_pushboolean(L, 0);  /* first result (false) */
    lua_pushvalue(L, -2);  /* error message */
    return 2;  /* return false, msg */
  }
  else
    return lua_gettop(L) - (int)extra;  /* return all results */
}

// 以保护模式调用一个函数. 也就是说f 中的任何错误不会抛出
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_pcall (lua_State *L) {
  int status;
  luaL_checkany(L, 1);
  lua_pushboolean(L, 1);  /* first result if no errors */
  lua_insert(L, 1);  /* put it in place */
  status = lua_pcallk(L, lua_gettop(L) - 2, LUA_MULTRET, 0, 0, finishpcall);
  return finishpcall(L, status, 0);
}


/*
** Do a protected call with error handling. After 'lua_rotate', the
** stack will have <f, err, true, f, [args...]>; so, the function passes
** 2 to 'finishpcall' to skip the 2 first values when returning results.
*/
// xpcall (f, msgh [, arg1, ···])
// 这个函数和 pcall 类似。 不过它可以额外设置一个消息处理器 msgh。
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_xpcall (lua_State *L) {
  int status;
  int n = lua_gettop(L);
  luaL_checktype(L, 2, LUA_TFUNCTION);  /* check error function */
  lua_pushboolean(L, 1);  /* first result */
  lua_pushvalue(L, 1);  /* function */
  lua_rotate(L, 3, 2);  /* move them below function's arguments */
  status = lua_pcallk(L, n - 2, LUA_MULTRET, 2, 2, finishpcall);
  return finishpcall(L, status, 2);
}

// tostring (v)
// 可以接收任何类型，它将其转换为人可阅读的字符串形式。 浮点数总被转换为浮点数的表现形式（小数点形式或是指数形式）。 
// （如果想完全控制数字如何被转换，可以使用 string.format。）
// 如果 v 有 "__tostring" 域的元表， tostring 会以 v 为参数调用它。 并用它的结果作为返回值。

// const char *luaL_tolstring (lua_State *L, int idx, size_t *len);
// 将给定索引处的 Lua 值转换为一个相应格式的 C 字符串。 结果串不仅会压栈，还会由函数返回。 如果 len 不为 NULL ， 它还把字符串长度设到 *len 中。
// -------------------------------------------------------------------------------------------------------------------------------------------
static int luaB_tostring (lua_State *L) {
  luaL_checkany(L, 1);
  luaL_tolstring(L, 1, NULL);
  return 1;
}


static const luaL_Reg base_funcs[] = {
  {"assert", luaB_assert},
  {"collectgarbage", luaB_collectgarbage},
  {"dofile", luaB_dofile},
  {"error", luaB_error},
  {"getmetatable", luaB_getmetatable},
  {"ipairs", luaB_ipairs},
  {"loadfile", luaB_loadfile},
  {"load", luaB_load},
#if defined(LUA_COMPAT_LOADSTRING)
  {"loadstring", luaB_load},
#endif
  {"next", luaB_next},
  {"pairs", luaB_pairs},
  {"pcall", luaB_pcall},
  {"print", luaB_print},
  {"rawequal", luaB_rawequal},
  {"rawlen", luaB_rawlen},
  {"rawget", luaB_rawget},
  {"rawset", luaB_rawset},
  {"select", luaB_select},
  {"setmetatable", luaB_setmetatable},
  {"tonumber", luaB_tonumber},
  {"tostring", luaB_tostring},
  {"type", luaB_type},
  {"xpcall", luaB_xpcall},
  /* placeholders */
  {"_G", NULL},
  {"_VERSION", NULL},
  {NULL, NULL}
};

// lua_pushglobaltable：将全局环境压栈。
// luaL_setfuncs：将base_funcs中所有的函数注册进栈顶的表中
// -------------------------------------------------------------------------------------------------------------------------------------------
LUAMOD_API int luaopen_base (lua_State *L) {
  /* open lib into global table */
  lua_pushglobaltable(L);
  luaL_setfuncs(L, base_funcs, 0);
  /* set global _G */
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "_G");
  /* set global _VERSION */
  lua_pushliteral(L, LUA_VERSION);
  lua_setfield(L, -2, "_VERSION");
  return 1;
}

