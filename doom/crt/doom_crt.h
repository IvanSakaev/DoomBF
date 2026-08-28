
#define NULL ((void*)0)

#ifndef _WIN32
typedef unsigned int size_t;
#endif

typedef unsigned long ulong;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef unsigned short u16;
typedef unsigned char u8;

typedef unsigned long long uint64_t;
typedef unsigned long long u_int64_t;
typedef unsigned int uint32_t;
typedef unsigned int u_int32_t;
typedef unsigned short uint16_t;
typedef unsigned short u_int16_t;
typedef unsigned char uint8_t;
typedef unsigned char u_int8_t;

typedef  int __int32_t;
typedef unsigned int __uint32_t;

typedef unsigned long long u_quad_t;    /* quads */
typedef long long quad_t;
typedef quad_t * qaddr_t;
typedef unsigned int u_int;
typedef unsigned int uint;

typedef long long int64_t;
typedef int int32_t;
typedef short int16_t;
typedef char int8_t;

#include <stdarg.h>
//#ifdef _WIN32
//#define va_start __crt_va_start
//#define va_arg   __crt_va_arg
//#define va_end   __crt_va_end
//#define va_copy(destination, source) ((destination) = (source))
//#else
//#define va_start(PTR, LASTARG)	__builtin_va_start (PTR, LASTARG)
//#define va_end(PTR)		__builtin_va_end (PTR)
//#define va_arg(PTR, TYPE)	__builtin_va_arg (PTR, TYPE)
//#define va_list			__builtin_va_list  
//#endif

int crtdoom_printf (const char *format, ...); 
int crtdoom_vprintf (const char *format, va_list ap);
int crtdoom_snprintf (char *str, size_t size, const char *format, ...);
int crtdoom_vsnprintf (char *str, size_t size, const char *format, va_list ap);


void *crtdoom_memset (void *addr, int val, int len);
void *crtdoom_memcpy (void *dest, const void *src, int len);
int crtdoom_strcmp (const char *s1, const char *s2);
int crtdoom_memcmp (const void *p1, const void *p2, int len);
int crtdoom_strlen (const char *p);
int crtdoom_strncmp (const char *s1, const char *s2, int len);

#define printf  crtdoom_printf 
#define vprintf  crtdoom_vprintf 
#define snprintf  crtdoom_snprintf
#define vsnprintf  crtdoom_vsnprintf 

#define memset  crtdoom_memset 
#define memcpy  crtdoom_memcpy 
#define strcmp  crtdoom_strcmp 
#define memcmp  crtdoom_memcmp 
#define strlen  crtdoom_strlen 
#define strncmp crtdoom_strncmp 
