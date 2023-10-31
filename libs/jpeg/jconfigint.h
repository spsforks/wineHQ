/* File modified by Torge Matthies for Wine. */

#include <stdarg.h>
#include <windef.h>
#include <winnt.h>

#define BUILD "20230516"
#undef inline
#undef INLINE
#define INLINE FORCEINLINE
#define THREAD_LOCAL __declspec(thread)
#define PACKAGE_NAME "libjpeg-turbo"
#define VERSION "2.1.91"
#define HAVE_INTRIN_H
#ifdef _WIN64
#define SIZEOF_SIZE_T 8
#undef HAVE_BUILTIN_CTZL
#else
#define SIZEOF_SIZE_T 4
#define HAVE_BUILTIN_CTZL
#define HAVE_BITSCANFORWARD
#endif
#define FALLTHROUGH
#ifndef BITS_IN_JSAMPLE
#define BITS_IN_JSAMPLE 8
#endif
#undef WITH_SIMD
#if BITS_IN_JSAMPLE == 8
#define C_ARITH_CODING_SUPPORTED 1
#define D_ARITH_CODING_SUPPORTED 1
#if defined(__i386__) || defined(__x86_64__)
#define WITH_SIMD 1
#endif
#else
#undef C_ARITH_CODING_SUPPORTED
#undef D_ARITH_CODING_SUPPORTED
#endif
