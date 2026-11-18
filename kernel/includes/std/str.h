#ifndef KERNEL_STR_H
#define KERNEL_STR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

static inline int strlen(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static inline int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static inline int strncmp(const char* a, const char* b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

static inline char* strcpy(char* dst, const char* src) {
    char* r = dst;
    while ((*dst++ = *src++)) {}
    return r;
}

static inline char* strncpy(char* dst, const char* src, uint32_t n) {
    uint32_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

static inline char* strchr(const char* s, int c) {
    do {
        if (*s == (char)c) return (char*)s;
    } while (*s++);
    return 0;
}

static inline char* strrchr(const char* s, int c) {
    const char* last = 0;
    do {
        if (*s == (char)c) last = s;
    } while (*s++);
    return (char*)last;
}

static inline char* strcat(char* dst, const char* src) {
    char* p = dst;
    while (*p) p++;
    while ((*p++ = *src++)) {}
    return dst;
}

static inline char* strncat(char* dst, const char* src, uint32_t n) {
    char* p = dst;
    while (*p) p++;
    uint32_t i = 0;
    for (; i < n && src[i]; i++) p[i] = src[i];
    p[i] = '\0';
    return dst;
}

static inline char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char*)haystack;
    }
    return 0;
}

static inline uint32_t strlcpy(char* dst, const char* src, uint32_t size) {
    uint32_t i = 0;
    if (size > 0) {
        for (; i < size - 1 && src[i]; i++) dst[i] = src[i];
        dst[i] = '\0';
    }
    while (src[i]) i++;
    return i;
}

static inline uint32_t strnlen(const char* s, uint32_t maxlen) {
    uint32_t n = 0;
    while (n < maxlen && s[n]) n++;
    return n;
}


static inline void* memset(void* p, int v, uint32_t n) {
    uint8_t* b = (uint8_t*)p;
    for (uint32_t i = 0; i < n; i++) b[i] = (uint8_t)v;
    return p;
}

static inline void* memcpy(void* dst, const void* src, uint32_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static inline int memcmp(const void* s1, const void* s2, uint32_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    for (uint32_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return (int)p1[i] - (int)p2[i];
    }
    return 0;
}

static inline void* memmove(void* dst, const void* src, uint32_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) {
        for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (uint32_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

static inline void* memchr(const void* s, int c, uint32_t n) {
    const uint8_t* p = (const uint8_t*)s;
    for (uint32_t i = 0; i < n; i++) {
        if (p[i] == (uint8_t)c) return (void*)(p + i);
    }
    return 0;
}


#ifdef __cplusplus
}
#endif

#endif