/*
** $Id: lctype.h,v 1.12 2011/07/15 12:50:29 roberto Exp $
** 'ctype' functions for Lua
** See Copyright Notice in lua.h
*/

#ifndef lctype_h
#define lctype_h

#include "lua.h"


/*
** WARNING: the functions defined here do not necessarily correspond
** to the similar functions in the standard C ctype.h. They are
** optimized for the specific needs of Lua
*/

#if !defined(LUA_USE_CTYPE)

#if 'A' == 65 && '0' == 48
/* ASCII case: can use its own tables; faster and fixed */
#define LUA_USE_CTYPE	0
#else
/* must use standard C ctype */
#define LUA_USE_CTYPE	1
#endif

#endif


#if !LUA_USE_CTYPE	/* { */

#include <limits.h>

#include "llimits.h"


#define ALPHABIT	0
#define DIGITBIT	1
#define PRINTBIT	2
#define SPACEBIT	3
#define XDIGITBIT	4


#define MASK(B)		(1 << (B))


/*
** add 1 to char to allow index -1 (EOZ)
*/
// 在luai_ctype_数组中一共存在7种值，分别是：（定义在lctype.c中）
//  - 0x04       对应的二进制为   00000100  
//  - 0x16       对应的二进制为   00010110
//  - 0x15       对应的二进制为   00010101
//  - 0x05       对应的二进制为   00000101
//  - 0x08       对应的二进制为   00001000
//  - 0x0c       对应的二进制为   00001100
//  - 0x00       对应的二进制为   00000000
#define testprop(c,p)	(luai_ctype_[(c)+1] & (p))

/*
** 'lalpha' (Lua alphabetic) and 'lalnum' (Lua alphanumeric) both include '_'
*/

// 下面的宏用作判断传进来的字符，判断是否为特定的类型，从名字上的意思就可以推断出
// 这里以lislalpha为例，传入一个char值，MASK偏移为0位，也就还是1，然后通过计算testprop值
// 在testprop中，从luai_ctype_数组中获取一个值和MASK进行与操作，然后得到一个值
// 在lislalpha中，总是与1进行与操作，1的二进制为00000001,根据相与的结果，
// 如果要使lislalpha返回非0（即为true）则，对应的数（二进制表示）末尾必须为1，以上满足lislalpha返回非0的值只有0x05,0x15,0x05,0x15的二进制表示，末尾为1。
// **luai_ctype_数组序列** 
// 66  -  71                      数值为：0x15
// 72  -  91                      数值为：0x05
// 98  -  103                     数值为：0x15
// 104 -  123                     数值为：0x05
// 在ascii表中的字母区间为：A-Z:65-90 a-z:97-122
// 所以在testprop宏中，需要把传进来的c加上1才对应的上luai_ctype_中的排序
// 上面就是有关判断c是否为字母的方法，类似的宏原理一样
#define lislalpha(c)	testprop(c, MASK(ALPHABIT))

// 判断是否为字母或数字
#define lislalnum(c)	testprop(c, (MASK(ALPHABIT) | MASK(DIGITBIT)))

// 判断是否为是数字
#define lisdigit(c)	testprop(c, MASK(DIGITBIT))

// 判断是否是空格
#define lisspace(c)	testprop(c, MASK(SPACEBIT))

// 判断是否是可打印字符
#define lisprint(c)	testprop(c, MASK(PRINTBIT))

// 判断是否是十六机制字符
#define lisxdigit(c)	testprop(c, MASK(XDIGITBIT))

/*
** this 'ltolower' only works for alphabetic characters
*/
// 此宏仅仅工作在c是字母字符的时候，作用是把大写字符转换为小写字符
// 'A' ^ 'a' 的结果始终都是32的二进制(0010 0000) 这也解释了下面这个宏为什么只能作用于字母字符
// 然后再拿着传进来的c字符和0010 0000作位或运算，如果是大写字符的话就相当于加上了32，如果是小写的话就相当于没有变化
// 这个写法真是有意思 呵呵见识了
#define ltolower(c)	((c) | ('A' ^ 'a'))


/* two more entries for 0 and -1 (EOZ) */
LUAI_DDEC const lu_byte luai_ctype_[UCHAR_MAX + 2];


#else			/* }{ */

/*
** use standard C ctypes
*/

#include <ctype.h>


#define lislalpha(c)	(isalpha(c) || (c) == '_')
#define lislalnum(c)	(isalnum(c) || (c) == '_')
#define lisdigit(c)	(isdigit(c))
#define lisspace(c)	(isspace(c))
#define lisprint(c)	(isprint(c))
#define lisxdigit(c)	(isxdigit(c))

#define ltolower(c)	(tolower(c))

#endif			/* } */

#endif

