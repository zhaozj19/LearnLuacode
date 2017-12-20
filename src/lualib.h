/*
** $Id: lualib.h,v 1.45 2017/01/12 17:14:26 roberto Exp $
** Lua standard libraries
** See Copyright Notice in lua.h
*/


#ifndef lualib_h
#define lualib_h

#include "lua.h"


/* version suffix for environment variable names */
#define LUA_VERSUFFIX          "_" LUA_VERSION_MAJOR "_" LUA_VERSION_MINOR

/*
	这里定义了一个lua版本号的宏，LUA_VERSION_MAJOR和LUA_VERSION_MINOR分别是5和3 定义在lua.h中
*/


LUAMOD_API int (luaopen_base) (lua_State *L);

/*
	LUAMOD_API是在luaconf.h定义的，类型为LUALIB_API
	代表这是外部库的API。类似的还有LUA_LIBAPI，LUA_API 其实归根结底都是extern
	extern表示变量或者函数的定义在别的文件中，提示编译器遇到此变量或函数时，在其它模块中寻找其定义
	所以luaopen_base定义不在这里 是在lbaselib.c里面。下面类似的函数也一样。
	这里声明了一个luaopen_base的函数,返回一个int数值，参数为lua_State的指针
*/

#define LUA_COLIBNAME	"coroutine"
LUAMOD_API int (luaopen_coroutine) (lua_State *L);

/*
	声明一个luaopen_coroutine的函数，返回值是一个int数值，参数为lua_State的指针
*/

#define LUA_TABLIBNAME	"table"
LUAMOD_API int (luaopen_table) (lua_State *L);

/*
	声明一个luaopen_table的函数，返回值是一个int数值，参数为lua_State的指针
*/

#define LUA_IOLIBNAME	"io"
LUAMOD_API int (luaopen_io) (lua_State *L);

/*
	声明一个luaopen_io的函数，返回值是一个int数值，参数为lua_State的指针
*/

#define LUA_OSLIBNAME	"os"
LUAMOD_API int (luaopen_os) (lua_State *L);

/*
	声明一个luaopen_os的函数，返回值是一个int数值，参数为lua_State的指针
*/

#define LUA_STRLIBNAME	"string"
LUAMOD_API int (luaopen_string) (lua_State *L);

/*
	声明一个luaopen_string的函数，返回值是一个int数值，参数为lua_State的指针
*/

#define LUA_UTF8LIBNAME	"utf8"
LUAMOD_API int (luaopen_utf8) (lua_State *L);

/*
	声明一个luaopen_utf8的函数，返回值是一个int数值，参数为lua_State的指针
*/

#define LUA_BITLIBNAME	"bit32"
LUAMOD_API int (luaopen_bit32) (lua_State *L);

/*
	声明一个luaopen_bit32的函数，返回值是一个int数值，参数为lua_State的指针
*/

#define LUA_MATHLIBNAME	"math"
LUAMOD_API int (luaopen_math) (lua_State *L);

/*
	声明一个luaopen_math的函数，返回值是一个int数值，参数为lua_State的指针
*/

#define LUA_DBLIBNAME	"debug"
LUAMOD_API int (luaopen_debug) (lua_State *L);

/*
	声明一个luaopen_debug的函数，返回值是一个int数值，参数为lua_State的指针
*/

#define LUA_LOADLIBNAME	"package"
LUAMOD_API int (luaopen_package) (lua_State *L);

/*
	声明一个luaopen_package的函数，返回值是一个int数值，参数为lua_State的指针
*/

/* open all previous libraries */
LUALIB_API void (luaL_openlibs) (lua_State *L);

/*
	声明一个luaL_openlibs的函数，返回值是一个int数值，参数为lua_State的指针
*/



#if !defined(lua_assert)
#define lua_assert(x)	((void)0)
#endif


#endif

// lualib.h 定义了标准库，用户可以包含这个文件，来统一openlibs，也可以根据自己的选择，在linit.c中加载部分lib（luaL_openlibs定义在linit.c中）