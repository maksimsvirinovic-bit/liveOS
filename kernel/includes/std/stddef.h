#ifndef _STDDEF_H
#define _STDDEF_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long size_t;
typedef signed long   ptrdiff_t;

#ifndef __cplusplus
typedef int wchar_t;
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

#define offsetof(type, member) __builtin_offsetof(type, member)

typedef struct {
    long long __ll;
    long double __ld;
    void* __p;
} max_align_t;

#ifdef __cplusplus
}
#endif

#endif /* _STDDEF_H */