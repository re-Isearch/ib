/*
#define WINDOWS 1
*/


#pragma GCC optimize("O3","unroll-loops","omit-frame-pointer","inline") //Optimization flags
#pragma GCC option("arch=native","tune=native","no-zero-upper") //Enable AVX
/*
#pragma GCC target("avx")  //Enable AVX
*/

#if defined(WIN32) || defined(WINDOWS) 
# ifndef _WIN32
#  define _WIN32
# endif
#endif

// #define  O_BUILD_IB32 1

#ifdef _WIN32
# include "conf_win32.h"
#else
# include "conf.h"
#endif

#ifndef IS_BIG_ENDIAN
# ifdef IS_LITTLE_ENDIAN
#   define IS_BIG_ENDIAN !(IS_LITTLE_ENDIAN)
# endif
#endif
#ifndef IS_LITTLE_ENDIAN
# ifdef IS_BIG_ENDIAN
#   define IS_LITTLE_ENDIAN !(IS_BIG_ENDIAN)
# endif
#endif

/*  Set some platform features */
#ifndef PLATFORM_INCLUDED
/*
 * Detect the address width, not the width of long.  Win64 uses LLP64:
 * pointers are 64-bit while long remains 32-bit.
 */
#if defined(_WIN64) || defined(__LP64__) || defined(_LP64) || \
    (defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ == 8)) || \
    (defined(SIZEOF_VOID_P) && (SIZEOF_VOID_P == 8)) || \
    (defined(SIZEOF_LONG_INT) && (SIZEOF_LONG_INT == 8))
# define HOST_MACHINE_64 1
#else
# define HOST_MACHINE_64 0
#endif
#define PLATFORM_INCLUDED 1
#endif
