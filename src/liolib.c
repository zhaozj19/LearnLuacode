/*
** $Id: liolib.c,v 2.151 2016/12/20 18:37:00 roberto Exp $
** Standard I/O (and system) library
** See Copyright Notice in lua.h
*/

#define liolib_c
#define LUA_LIB

#include "lprefix.h"


#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"




/*
** Change this macro to accept other modes for 'fopen' besides
** the standard ones.
*/
#if !defined(l_checkmode)

/* accepted extensions to 'mode' in 'fopen' */
#if !defined(L_MODEEXT)
#define L_MODEEXT	"b"
#endif

/* Check whether 'mode' matches '[rwa]%+?[L_MODEEXT]*' */
// strchr是一个C函数，原型是：char *strchr(const char* _Str,char _Val) 用处是查找字符串_Str中首次出现字符_Val的位置
// 返回首次出现_Val的位置的指针，返回的地址是被查找字符串指针开始的第一个与Val相同字符的指针，如果Str中不存在Val则返回NULL。
// strspn是一个C函数，原型是：size_t strspn (const char *s,const char * accept);用处是从参数s 字符串的开头计算连续的字符，而这些字符都完全是accept 所指字符串中的字符。
// 返回字符串s开头连续包含字符串accept内的字符数目
// 这个宏就是检查传进来的mode参数符不符合规范，具体规则是判断mode是否为空、是否包含'rwa字符'
static int l_checkmode (const char *mode) {
  return (*mode != '\0' && strchr("rwa", *(mode++)) != NULL &&
         (*mode != '+' || (++mode, 1)) &&  /* skip if char is '+' */
         (strspn(mode, L_MODEEXT) == strlen(mode)));  /* check extensions */
}

#endif

/*
** {======================================================
** l_popen spawns a new process connected to the current
** one through the file streams.
** =======================================================
*/
// l_popen构造一个新进程，通过文件流的方连接到当前进程

#if !defined(l_popen)		/* { */

#if defined(LUA_USE_POSIX)	/* { */
// 下面是POSIX规范下的定义
// fflush:清除读写缓冲区，需要立即把输出缓冲区的数据进行物理写入时
#define l_popen(L,c,m)		(fflush(NULL), popen(c,m))
#define l_pclose(L,file)	(pclose(file))
// POSIX规范结束

#elif defined(LUA_USE_WINDOWS)	/* }{ */
// 下面是windows下的定义
// _popen通过创建一个管道，调用 fork 产生一个子进程。这个进程必须由 _pclose() 函数关闭，而不是 fclose() 函数
// 管道就是一部份共享内存以便进程可以用来相互通信
// 原型为：FILE * popen ( const char * command , const char * type );
// type 参数只能是读或者写中的一种。如果 type 是 "r" 则文件指针连接到 command 的标准输出；如果 type 是 "w" 则文件指针连接到 command 的标准输入。
// command 参数是一个指向以 NULL 结束的 shell 命令字符串的指针。这行命令将被传到 bin/sh 并使用-c 标志，shell 将执行这个命令。
#define l_popen(L,c,m)		(_popen(c,m))
// pclose()用来关闭由popen所建立的管道及文件指针。参数stream为先前由popen()所返回的文件指针
// 若成功返回shell的终止状态(也即子进程的终止状态)，若出错返回-1，错误原因存于errno中
#define l_pclose(L,file)	(_pclose(file))
// windows规范结束

#else				/* }{ */

/* ISO C definitions */
#define l_popen(L,c,m)  \
	  ((void)((void)c, m), \
	  luaL_error(L, "'popen' not supported"), \
	  (FILE*)0)
#define l_pclose(L,file)		((void)L, (void)file, -1)

#endif				/* } */

#endif				/* } */

/* }====================================================== */


#if !defined(l_getc)		/* { */

#if defined(LUA_USE_POSIX)
#define l_getc(f)		getc_unlocked(f)
#define l_lockfile(f)		flockfile(f)
#define l_unlockfile(f)		funlockfile(f)
#else
#define l_getc(f)		getc(f)
#define l_lockfile(f)		((void)0)
#define l_unlockfile(f)		((void)0)
#endif

#endif				/* } */


/*
** {======================================================
** l_fseek: configuration for longer offsets
** =======================================================
*/

#if !defined(l_fseek)		/* { */

#if defined(LUA_USE_POSIX)	/* { */

#include <sys/types.h>

#define l_fseek(f,o,w)		fseeko(f,o,w)
#define l_ftell(f)		ftello(f)
#define l_seeknum		off_t

#elif defined(LUA_USE_WINDOWS) && !defined(_CRTIMP_TYPEINFO) \
   && defined(_MSC_VER) && (_MSC_VER >= 1400)	/* }{ */

/* Windows (but not DDK) and Visual C++ 2005 or higher */
#define l_fseek(f,o,w)		_fseeki64(f,o,w)
#define l_ftell(f)		_ftelli64(f)
#define l_seeknum		__int64

#else				/* }{ */

/* ISO C definitions */
#define l_fseek(f,o,w)		fseek(f,o,w)
#define l_ftell(f)		ftell(f)
#define l_seeknum		long

#endif				/* } */

#endif				/* } */

/* }====================================================== */


#define IO_PREFIX	"_IO_"
#define IOPREF_LEN	(sizeof(IO_PREFIX)/sizeof(char) - 1)
#define IO_INPUT	(IO_PREFIX "input")
#define IO_OUTPUT	(IO_PREFIX "output")


typedef luaL_Stream LStream;

// luaL_checkudata判断栈中ud索引的值是否是第三个参数里面的数据类型，是的话返回地址，否则抛出错误
#define tolstream(L)	((LStream *)luaL_checkudata(L, 1, LUA_FILEHANDLE))

// 判断文件是否已经被关闭，是看处理文件的C函数是否为NULL
#define isclosed(p)	((p)->closef == NULL)



// 文件描述符在形式上是一个非负整数。实际上，它是一个索引值，指向内核为每一个进程所维护的该进程打开文件的记录表。
// 当程序打开一个现有文件或者创建一个新文件时，内核向进程返回一个文件描述符。
// 在程序设计中，一些涉及底层的程序编写往往会围绕着文件描述符展开。但是文件描述符这一概念往往只适用于UNIX、Linux这样的操作系统。
// 在非UNIX/Linux 操作系统上（如Windows），无法基于这一概念进行编程——事实上，Windows下的文件描述符和信号量、互斥锁等内核对象一样都记作HANDLE。（以上摘自维基百科）

// 而在lua中，作者把文件描述符抽象成了luaL_Stream数据结构(定义在lauxlib.h中)

// 检测一个文件描述符obj是否有效，假如obj是一个打开的文件描述符则会返回字符串"file"
// 如果obj是一个关闭的文件描述符则会返回"closed file"
// 当obj是一个无效的文件描述符时则会返回nil

// luaL_checkany检测index处的参数是否有类型(包括nil),没有的话就报错
// luaL_testudata(L, 1, LUA_FILEHANDLE)检查栈中第1个参数是否为类型为LUA_FILEHANDLE的用户数据
// LUA_FILEHANDLE是定义在lauxlib.h中FILE*文件指针类型
// lua_pushliteral向lua栈中压入一个字符串
static int io_type (lua_State *L) {
  LStream *p;
  luaL_checkany(L, 1);
  p = (LStream *)luaL_testudata(L, 1, LUA_FILEHANDLE);
  if (p == NULL)
    lua_pushnil(L);  /* not a file */
  else if (isclosed(p))
    lua_pushliteral(L, "closed file");
  else
    lua_pushliteral(L, "file");
  return 1;
}

// 打印文件的FILE*指针
static int f_tostring (lua_State *L) {
  LStream *p = tolstream(L);
  if (isclosed(p))
    lua_pushliteral(L, "file (closed)");
  else
    lua_pushfstring(L, "file (%p)", p->f);
  return 1;
}

// 返回L栈底的文件类型的userdata的指针
static FILE *tofile (lua_State *L) {
  LStream *p = tolstream(L);
  if (isclosed(p))
    luaL_error(L, "attempt to use a closed file");
  lua_assert(p->f);
  return p->f;
}


/*
** When creating file handles, always creates a 'closed' file handle
** before opening the actual file; so, if there is a memory error, the
** handle is in a consistent state.
*/
// 当创建一个文件句柄时，在打开任何实际的文件时总是创建一个关闭的文件句柄，因此如果出现内存错误，则句柄处于一致状态。
// newprefile创建一个lua中的文件句柄(文件也属于用户数据)
// 创建初的句柄为关闭状态
// 然后调用luaL_setmetatable，从注册表中获取key为FILE*的元表，然后设置为p的元表
static LStream *newprefile (lua_State *L) {
  LStream *p = (LStream *)lua_newuserdata(L, sizeof(LStream));
  p->closef = NULL;  /* mark file handle as 'closed' */
  luaL_setmetatable(L, LUA_FILEHANDLE);
  return p;
}


/*
** Calls the 'close' function from a file handle. The 'volatile' avoids
** a bug in some versions of the Clang compiler (e.g., clang 3.0 for
** 32 bits).
*/
// 从一个文件句柄中调用'close'函数。'volatile'避免了在某些版本的Clang编译器中的一个bug(例如32位版本的clang3.0编译器)
// 通过tolstream返回栈底的文件描述，然后cf拿到文件描述的C函数closef，最后置为NULL就表示关闭了
static int aux_close (lua_State *L) {
  LStream *p = tolstream(L);
  volatile lua_CFunction cf = p->closef;
  p->closef = NULL;  /* mark stream as closed */
  return (*cf)(L);  /* close it */
}

// 关闭一个文件句柄(默认关闭标准输出文件)
// 这里通过判断调用io_close时有没有附加参数，没有的话就默认关闭标准输出文件
// lua_getfield的作用是把t[IO_OUTPUT]的值压入栈，t是LUA_REGISTRYINDEX所指的t
// 在这里，tofile的作用虽然是返回文件描述的FILE*指针，但是没有接收的变量，真实的作用是判断此文件是否为打开状态
static int io_close (lua_State *L) {
  if (lua_isnone(L, 1))  /* no argument? */
    lua_getfield(L, LUA_REGISTRYINDEX, IO_OUTPUT);  /* use standard output */
  tofile(L);  /* make sure argument is an open stream */
  return aux_close(L);
}


// 关闭一个文件句柄
static int f_gc (lua_State *L) {
  LStream *p = tolstream(L);
  if (!isclosed(p) && p->f != NULL)
    aux_close(L);  /* ignore closed and incompletely open files */
  return 0;
}


/*
** function to close regular files
*/
// 用来关闭文件描述符的
// 首先拿到文件描述符p，然后调用C函数fclose关闭FILE*指针
// 如果流成功关闭，fclose 返回 0，否则返回EOF（-1）
static int io_fclose (lua_State *L) {
  LStream *p = tolstream(L);
  int res = fclose(p->f);
  return luaL_fileresult(L, (res == 0), NULL);
}


// 创建一个文件，返回文件描述符
// 把文件描述符p的f置为NULL(f代表FILE*)，管理文件的C函数置为io_fclose
static LStream *newfile (lua_State *L) {
  LStream *p = newprefile(L);
  p->f = NULL;
  p->closef = &io_fclose;
  return p;
}

// 判断fname是否可以按照mode方式操作，不可以的话抛出错误
static void opencheck (lua_State *L, const char *fname, const char *mode) {
  LStream *p = newfile(L);
  p->f = fopen(fname, mode);
  if (p->f == NULL)
    luaL_error(L, "cannot open file '%s' (%s)", fname, strerror(errno));
}


// 此函数会以参数mode(调用io.open的第二个参数)所描述的方式打开文件filename，并返回一个文件描述符
// mode的类型及含义如下：
// 'r':以只读的方式打开文件，该文件必须存在，否则返回nil
// 'w':以只写方式打开文件，若文件存在则清空文件内容，若文件不存在则建立该文件，从头开始写
// 'a':以附加的方式打开只写文件。若文件不存在则会建立该文件，如果文件存在，写入的数据则会追加到文件尾
// 'r+':以读写方式打开文件，该文件必须存在，读取时从文件头开始读，写入时从文件头开始写，保留原文件中没有被覆盖的内容
// 'w+':以读写方式打开文件，若文件存在则清空文件内容。若文件不存在则建立该文件，从头写入，从头读取
// 'a+':以附加的方式打开可读写文件。若文件不存在，则会建立该文件，如果文件存在，写入的数据则会被追加到文件尾，原文件的内容会被保留
// 默认是以'r'的方式打开文件，紧接着创建一个新的文件描述符，然后拿到mode对其进行检测是否符合规范
// luaL_argcheck：检查第二个参数是否为真，如果不是的话抛出附带的错误消息
// fopen是一个C函数；函数原型：FILE * fopen(const char * path, const char * mode);
// 参数 path字符串包含欲打开的文件路径及文件名，参数 mode 字符串则代表着流形态。
// 文件顺利打开后，指向该流的文件指针就会被返回。如果文件打开失败则返回 NULL，并把错误代码存在 error 中。
// 然后文件描述符的FILE*字段就等于通过fopen返回指向该流的文件指针
// 最后成功的话返回1,不然就返回nil
static int io_open (lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  const char *mode = luaL_optstring(L, 2, "r");
  LStream *p = newfile(L);
  const char *md = mode;  /* to traverse/check mode */
  luaL_argcheck(L, l_checkmode(md), 2, "invalid mode");
  p->f = fopen(filename, mode);
  return (p->f == NULL) ? luaL_fileresult(L, 0, filename) : 1;
}


/*
** function to close 'popen' files
*/
// 这个函数不同于io_fclose,次函数是配套io_popen使用的，因为通过io_popen创建出来的文件描述符是可以连接进程的，所以不能混用
// 首先拿到文件描述符，然后调用l_pclose关闭此文件描述符，最后通过luaL_execresult将执行的结果保存在栈中(return true/nil,what,code)
static int io_pclose (lua_State *L) {
  LStream *p = tolstream(L);
  return luaL_execresult(L, l_pclose(L, p->f));
}



// 此函数用于在额外的进程中启动程序filename，并返回filename的句柄
// 通俗的来说就是使用这个函数可以调用一个命令（程序），并且返回一个和这个程序相关的文件描述符
// 首先获取程序的文件名filename，然后获取mode，默认是'r'，紧接着创建一个新的文件描述符p，然后p的FILE*指向l_popen的返回结果(一个文件描述符，可以连通两个进程)
// 然后管理文件的C函数置为io_pclose，最后成功的话返回1,不然就返回nil
static int io_popen (lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  const char *mode = luaL_optstring(L, 2, "r");
  LStream *p = newprefile(L);
  p->f = l_popen(L, filename, mode);
  p->closef = &io_pclose;
  return (p->f == NULL) ? luaL_fileresult(L, 0, filename) : 1;
}


// 此函数用来创建一个临时文件，并且返回临时文件的句柄
// 和创建一个文件一样，也是先创建一个LStream*类型的文件指针，不同的地方在于FILE*指针指向的是一个临时文件，哈哈哈哈
// 重点就是tmpfile，这是一个C函数，原型为：FILE *tmpfile(void);以'wb+'形式创建一个临时二进制文件，返回临时文件的文件句柄
// 创建出来的临时文件在程序结束时就会被自动删除
// 另外一个类似的函数，os.tmpname()只返回文件名，需要手动打开和关闭，而io.tmpfile()函数实现打开和关闭都是自动的
static int io_tmpfile (lua_State *L) {
  LStream *p = newfile(L);
  p->f = tmpfile();
  return (p->f == NULL) ? luaL_fileresult(L, 0, NULL) : 1;
}


static FILE *getiofile (lua_State *L, const char *findex) {
  LStream *p;
  lua_getfield(L, LUA_REGISTRYINDEX, findex);
  p = (LStream *)lua_touserdata(L, -1);
  if (isclosed(p))
    luaL_error(L, "standard %s file is closed", findex + IOPREF_LEN);
  return p->f;
}

// lua_isnoneornil用来判断L中索引处的值是否存在类型包含nil，如果不存在的话，返回false
// lua_tostring把L中索引处的字符串转化为C风格并返回
// 如果传了filename的话，那么调用opencheck对文件执行mode操作
// 如果没传的话，默认对当前默认文件进行操作
// 然后用tofile检测一下文件句柄是否合法，紧接着把句柄入栈
static int g_iofile (lua_State *L, const char *f, const char *mode) {
  if (!lua_isnoneornil(L, 1)) {
    const char *filename = lua_tostring(L, 1);
    if (filename)
      opencheck(L, filename, mode);
    else {
      tofile(L);  /* check that it's a valid file handle */
      lua_pushvalue(L, 1);
    }
    lua_setfield(L, LUA_REGISTRYINDEX, f);
  }
  /* return current value */
  lua_getfield(L, LUA_REGISTRYINDEX, f);
  return 1;
}


// 打开一个文件，然后把这个文件作为默认的输入文件，如果不传参数的话，返回当前默认的输入文件描述符
static int io_input (lua_State *L) {
  return g_iofile(L, IO_INPUT, "r");
}

// 打开一个文件，把这个文件作为默认的输出文件，如果不传参数的话，返回当前默认的输出文件描述符
static int io_output (lua_State *L) {
  return g_iofile(L, IO_OUTPUT, "w");
}


static int io_readline (lua_State *L);


/*
** maximum number of arguments to 'f:lines'/'io.lines' (it + 3 must fit
** in the limit for upvalues of a closure)
*/
#define MAXARGLINE	250


// lua_pushboolean:把 b 作为一个布尔量压栈(0为false，其他为true)
// lua_rotate(L,idx,n)：把从 idx 开始到栈顶的元素轮转 n 个位置。 对于 n 为正数时，轮转方向是向栈顶的； 当 n 为负数时，向栈底方向轮转 -n 个位置。（看不到说的什么，百度过来的）
// lua_pushcclosure：向栈中放入一个C闭包，当创建了一个 C 函数后， 你可以给它关联一些值， 这就是在创建一个 C 闭包
// 接下来无论函数何时被调用，这些值都可以被这个函数访问到。 
// 为了将一些值关联到一个 C 函数上， 首先这些值需要先被压入堆栈（如果有多个值，第一个先压）。 
// 接下来调用 lua_pushcclosure 来创建出闭包并把这个 C 函数压到栈上。 参数 n 告之函数有多少个值需要关联到函数上。 lua_pushcclosure 也会把这些值从栈上弹出。
static void aux_lines (lua_State *L, int toclose) {
  int n = lua_gettop(L) - 1;  /* number of arguments to read */
  luaL_argcheck(L, n <= MAXARGLINE, MAXARGLINE + 2, "too many arguments");
  lua_pushinteger(L, n);  /* number of arguments to read */
  lua_pushboolean(L, toclose);  /* close/not close file when finished */
  lua_rotate(L, 2, 2);  /* move 'n' and 'toclose' to their positions */
  lua_pushcclosure(L, io_readline, 3 + n);
}


static int f_lines (lua_State *L) {
  tofile(L);  /* check that it's a valid file handle */
  aux_lines(L, 0);
  return 1;
}

// 按照文件名以读的方式打开一个文件，并且返回一个迭代函数，这个迭代函数每一次被调用都会返回文件中新一行的内容
// 当迭代到文件末尾的时候，它会返回nil来结束循环并自动关闭文件
// toclose为0时，迭代完不关闭文件，为1时迭代完关闭文件
// 首先判断io.lines有没有传递参数，没有的话就是打开一个默认的输入文件，如果传递的有专门的文件名的话就打开它
// 紧接着就是调用lua_replace把栈顶的值压入栈底，最后调用aux_lines，且返回文件描述符
static int io_lines (lua_State *L) {
  int toclose;
  if (lua_isnone(L, 1)) lua_pushnil(L);  /* at least one argument */
  if (lua_isnil(L, 1)) {  /* no file name? */
    lua_getfield(L, LUA_REGISTRYINDEX, IO_INPUT);  /* get default input */
    lua_replace(L, 1);  /* put it at index 1 */
    tofile(L);  /* check that it's a valid file handle */
    toclose = 0;  /* do not close it after iteration */
  }
  else {  /* open a new file */
    const char *filename = luaL_checkstring(L, 1);
    opencheck(L, filename, "r");
    lua_replace(L, 1);  /* put file at index 1 */
    toclose = 1;  /* close it after iteration */
  }
  aux_lines(L, toclose);
  return 1;
}


/*
** {======================================================
** READ
** =======================================================
*/


/* maximum length of a numeral */
#if !defined (L_MAXLENNUM)
#define L_MAXLENNUM     200
#endif


/* auxiliary structure used by 'read_number' */
typedef struct {
  FILE *f;  /* file being read */
  int c;  /* current character (look ahead) */
  int n;  /* number of elements in buffer 'buff' */
  char buff[L_MAXLENNUM + 1];  /* +1 for ending '\0' */
} RN;


/*
** Add current char to buffer (if not out of space) and read next one
*/
static int nextc (RN *rn) {
  if (rn->n >= L_MAXLENNUM) {  /* buffer overflow? */
    rn->buff[0] = '\0';  /* invalidate result */
    return 0;  /* fail */
  }
  else {
    rn->buff[rn->n++] = rn->c;  /* save current char */
    rn->c = l_getc(rn->f);  /* read next one */
    return 1;
  }
}


/*
** Accept current char if it is in 'set' (of size 2)
*/
static int test2 (RN *rn, const char *set) {
  if (rn->c == set[0] || rn->c == set[1])
    return nextc(rn);
  else return 0;
}


/*
** Read a sequence of (hex)digits
*/
static int readdigits (RN *rn, int hex) {
  int count = 0;
  while ((hex ? isxdigit(rn->c) : isdigit(rn->c)) && nextc(rn))
    count++;
  return count;
}


/*
** Read a number: first reads a valid prefix of a numeral into a buffer.
** Then it calls 'lua_stringtonumber' to check whether the format is
** correct and to convert it to a Lua number
*/
static int read_number (lua_State *L, FILE *f) {
  RN rn;
  int count = 0;
  int hex = 0;
  char decp[2];
  rn.f = f; rn.n = 0;
  decp[0] = lua_getlocaledecpoint();  /* get decimal point from locale */
  decp[1] = '.';  /* always accept a dot */
  l_lockfile(rn.f);
  do { rn.c = l_getc(rn.f); } while (isspace(rn.c));  /* skip spaces */
  test2(&rn, "-+");  /* optional signal */
  if (test2(&rn, "00")) {
    if (test2(&rn, "xX")) hex = 1;  /* numeral is hexadecimal */
    else count = 1;  /* count initial '0' as a valid digit */
  }
  count += readdigits(&rn, hex);  /* integral part */
  if (test2(&rn, decp))  /* decimal point? */
    count += readdigits(&rn, hex);  /* fractional part */
  if (count > 0 && test2(&rn, (hex ? "pP" : "eE"))) {  /* exponent mark? */
    test2(&rn, "-+");  /* exponent signal */
    readdigits(&rn, 0);  /* exponent digits */
  }
  ungetc(rn.c, rn.f);  /* unread look-ahead char */
  l_unlockfile(rn.f);
  rn.buff[rn.n] = '\0';  /* finish string */
  if (lua_stringtonumber(L, rn.buff))  /* is this a valid number? */
    return 1;  /* ok */
  else {  /* invalid format */
   lua_pushnil(L);  /* "result" to be removed */
   return 0;  /* read fails */
  }
}


static int test_eof (lua_State *L, FILE *f) {
  int c = getc(f);
  ungetc(c, f);  /* no-op when c == EOF */
  lua_pushliteral(L, "");
  return (c != EOF);
}


static int read_line (lua_State *L, FILE *f, int chop) {
  luaL_Buffer b;
  int c = '\0';
  luaL_buffinit(L, &b);
  while (c != EOF && c != '\n') {  /* repeat until end of line */
    char *buff = luaL_prepbuffer(&b);  /* preallocate buffer */
    int i = 0;
    l_lockfile(f);  /* no memory errors can happen inside the lock */
    while (i < LUAL_BUFFERSIZE && (c = l_getc(f)) != EOF && c != '\n')
      buff[i++] = c;
    l_unlockfile(f);
    luaL_addsize(&b, i);
  }
  if (!chop && c == '\n')  /* want a newline and have one? */
    luaL_addchar(&b, c);  /* add ending newline to result */
  luaL_pushresult(&b);  /* close buffer */
  /* return ok if read something (either a newline or something else) */
  return (c == '\n' || lua_rawlen(L, -1) > 0);
}


static void read_all (lua_State *L, FILE *f) {
  size_t nr;
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  do {  /* read file in chunks of LUAL_BUFFERSIZE bytes */
    char *p = luaL_prepbuffer(&b);
    nr = fread(p, sizeof(char), LUAL_BUFFERSIZE, f);
    luaL_addsize(&b, nr);
  } while (nr == LUAL_BUFFERSIZE);
  luaL_pushresult(&b);  /* close buffer */
}


static int read_chars (lua_State *L, FILE *f, size_t n) {
  size_t nr;  /* number of chars actually read */
  char *p;
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  p = luaL_prepbuffsize(&b, n);  /* prepare buffer to read whole block */
  nr = fread(p, sizeof(char), n, f);  /* try to read 'n' chars */
  luaL_addsize(&b, nr);
  luaL_pushresult(&b);  /* close buffer */
  return (nr > 0);  /* true iff read something */
}


static int g_read (lua_State *L, FILE *f, int first) {
  int nargs = lua_gettop(L) - 1;
  int success;
  int n;
  clearerr(f);
  if (nargs == 0) {  /* no arguments? */
    success = read_line(L, f, 1);
    n = first+1;  /* to return 1 result */
  }
  else {  /* ensure stack space for all results and for auxlib's buffer */
    luaL_checkstack(L, nargs+LUA_MINSTACK, "too many arguments");
    success = 1;
    for (n = first; nargs-- && success; n++) {
      if (lua_type(L, n) == LUA_TNUMBER) {
        size_t l = (size_t)luaL_checkinteger(L, n);
        success = (l == 0) ? test_eof(L, f) : read_chars(L, f, l);
      }
      else {
        const char *p = luaL_checkstring(L, n);
        if (*p == '*') p++;  /* skip optional '*' (for compatibility) */
        switch (*p) {
          case 'n':  /* number */
            success = read_number(L, f);
            break;
          case 'l':  /* line */
            success = read_line(L, f, 1);
            break;
          case 'L':  /* line with end-of-line */
            success = read_line(L, f, 0);
            break;
          case 'a':  /* file */
            read_all(L, f);  /* read entire file */
            success = 1; /* always success */
            break;
          default:
            return luaL_argerror(L, n, "invalid format");
        }
      }
    }
  }
  if (ferror(f))
    return luaL_fileresult(L, 0, NULL);
  if (!success) {
    lua_pop(L, 1);  /* remove last result */
    lua_pushnil(L);  /* push nil instead */
  }
  return n - first;
}


static int io_read (lua_State *L) {
  return g_read(L, getiofile(L, IO_INPUT), 1);
}


static int f_read (lua_State *L) {
  return g_read(L, tofile(L), 2);
}

// lua_upvalueindex返回当前运行的函数的第 i 个上值的伪索引
static int io_readline (lua_State *L) {
  LStream *p = (LStream *)lua_touserdata(L, lua_upvalueindex(1));
  int i;
  int n = (int)lua_tointeger(L, lua_upvalueindex(2));
  if (isclosed(p))  /* file is already closed? */
    return luaL_error(L, "file is already closed");
  lua_settop(L , 1);
  luaL_checkstack(L, n, "too many arguments");
  for (i = 1; i <= n; i++)  /* push arguments to 'g_read' */
    lua_pushvalue(L, lua_upvalueindex(3 + i));
  n = g_read(L, p->f, 2);  /* 'n' is number of results */
  lua_assert(n > 0);  /* should return at least a nil */
  if (lua_toboolean(L, -n))  /* read at least one value? */
    return n;  /* return them */
  else {  /* first result is nil: EOF or error */
    if (n > 1) {  /* is there error information? */
      /* 2nd result is error message */
      return luaL_error(L, "%s", lua_tostring(L, -n + 1));
    }
    if (lua_toboolean(L, lua_upvalueindex(3))) {  /* generator created file? */
      lua_settop(L, 0);
      lua_pushvalue(L, lua_upvalueindex(1));
      aux_close(L);  /* close it */
    }
    return 0;
  }
}

/* }====================================================== */


static int g_write (lua_State *L, FILE *f, int arg) {
  int nargs = lua_gettop(L) - arg;
  int status = 1;
  for (; nargs--; arg++) {
    if (lua_type(L, arg) == LUA_TNUMBER) {
      /* optimization: could be done exactly as for strings */
      int len = lua_isinteger(L, arg)
                ? fprintf(f, LUA_INTEGER_FMT,
                             (LUAI_UACINT)lua_tointeger(L, arg))
                : fprintf(f, LUA_NUMBER_FMT,
                             (LUAI_UACNUMBER)lua_tonumber(L, arg));
      status = status && (len > 0);
    }
    else {
      size_t l;
      const char *s = luaL_checklstring(L, arg, &l);
      status = status && (fwrite(s, sizeof(char), l, f) == l);
    }
  }
  if (status) return 1;  /* file handle already on stack top */
  else return luaL_fileresult(L, status, NULL);
}


static int io_write (lua_State *L) {
  return g_write(L, getiofile(L, IO_OUTPUT), 1);
}


static int f_write (lua_State *L) {
  FILE *f = tofile(L);
  lua_pushvalue(L, 1);  /* push file at the stack top (to be returned) */
  return g_write(L, f, 2);
}


static int f_seek (lua_State *L) {
  static const int mode[] = {SEEK_SET, SEEK_CUR, SEEK_END};
  static const char *const modenames[] = {"set", "cur", "end", NULL};
  FILE *f = tofile(L);
  int op = luaL_checkoption(L, 2, "cur", modenames);
  lua_Integer p3 = luaL_optinteger(L, 3, 0);
  l_seeknum offset = (l_seeknum)p3;
  luaL_argcheck(L, (lua_Integer)offset == p3, 3,
                  "not an integer in proper range");
  op = l_fseek(f, offset, mode[op]);
  if (op)
    return luaL_fileresult(L, 0, NULL);  /* error */
  else {
    lua_pushinteger(L, (lua_Integer)l_ftell(f));
    return 1;
  }
}


static int f_setvbuf (lua_State *L) {
  static const int mode[] = {_IONBF, _IOFBF, _IOLBF};
  static const char *const modenames[] = {"no", "full", "line", NULL};
  FILE *f = tofile(L);
  int op = luaL_checkoption(L, 2, NULL, modenames);
  lua_Integer sz = luaL_optinteger(L, 3, LUAL_BUFFERSIZE);
  int res = setvbuf(f, NULL, mode[op], (size_t)sz);
  return luaL_fileresult(L, res == 0, NULL);
}



static int io_flush (lua_State *L) {
  return luaL_fileresult(L, fflush(getiofile(L, IO_OUTPUT)) == 0, NULL);
}


static int f_flush (lua_State *L) {
  return luaL_fileresult(L, fflush(tofile(L)) == 0, NULL);
}


/*
** functions for 'io' library
*/
static const luaL_Reg iolib[] = {
  {"close", io_close},
  {"flush", io_flush},
  {"input", io_input},
  {"lines", io_lines},
  {"open", io_open},
  {"output", io_output},
  {"popen", io_popen},
  {"read", io_read},
  {"tmpfile", io_tmpfile},
  {"type", io_type},
  {"write", io_write},
  {NULL, NULL}
};


/*
** methods for file handles
*/
static const luaL_Reg flib[] = {
  {"close", io_close},
  {"flush", f_flush},
  {"lines", f_lines},
  {"read", f_read},
  {"seek", f_seek},
  {"setvbuf", f_setvbuf},
  {"write", f_write},
  {"__gc", f_gc},
  {"__tostring", f_tostring},
  {NULL, NULL}
};


static void createmeta (lua_State *L) {
  luaL_newmetatable(L, LUA_FILEHANDLE);  /* create metatable for file handles */
  lua_pushvalue(L, -1);  /* push metatable */
  lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
  luaL_setfuncs(L, flib, 0);  /* add file methods to new metatable */
  lua_pop(L, 1);  /* pop new metatable */
}


/*
** function to (not) close the standard files stdin, stdout, and stderr
*/
static int io_noclose (lua_State *L) {
  LStream *p = tolstream(L);
  p->closef = &io_noclose;  /* keep file opened */
  lua_pushnil(L);
  lua_pushliteral(L, "cannot close standard file");
  return 2;
}


static void createstdfile (lua_State *L, FILE *f, const char *k,
                           const char *fname) {
  LStream *p = newprefile(L);
  p->f = f;
  p->closef = &io_noclose;
  if (k != NULL) {
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, k);  /* add file to registry */
  }
  lua_setfield(L, -2, fname);  /* add file to module */
}


LUAMOD_API int luaopen_io (lua_State *L) {
  luaL_newlib(L, iolib);  /* new module */
  createmeta(L);
  /* create (and set) default files */
  createstdfile(L, stdin, IO_INPUT, "stdin");
  createstdfile(L, stdout, IO_OUTPUT, "stdout");
  createstdfile(L, stderr, NULL, "stderr");
  return 1;
}

