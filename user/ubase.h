#ifndef UBASE
#define UBASE

#include "sys.h"

#define NULL ((void*)0)

static inline int strlen_(const char* s) {
    if (s == NULL) return -1;
    int n = 0;
    while (s[n]) n++;
    return n;
}
static inline int strcmp_(const char* a, const char* b) {
    if (a == NULL || b == NULL) return -1;
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static inline void strcpy_(char* d, const char* s) {
    if (d == NULL || s == NULL) return;
    while ((*d++ = *s++)) {}
}

static inline void print(const char* s) { sys_write(1, s, strlen_(s)); }
static inline void print_int(int value) {
    char digits[12];
    int count = 0;
    unsigned int number;
    if (value < 0) {
        print("-");
        number = (unsigned int)(-(value + 1)) + 1;
    } else {
        number = (unsigned int)value;
    }
    do {
        digits[count++] = '0' + number % 10;
        number /= 10;
    } while (number);
    while (count) {
        char ch[2] = { digits[--count], '\0' };
        print(ch);
    }
}

#endif