#ifndef KERNEL_MUTEX_H
#define KERNEL_MUTEX_H

#include <stdint.h>
#include "tasking/process.h"

typedef struct WaitNode {
    Process* p;
    struct WaitNode* next;
} WaitNode;

typedef struct {
    volatile uint8_t locked;
    Process*         owner;
    WaitNode*        waiters;
} mutex_t;

#define MUTEX_INIT { 0, 0 }

static inline void mutex_init(mutex_t* m) {
    m->locked = 0;
    m->owner = 0;
}

void mutex_lock(mutex_t* m);
void mutex_unlock(mutex_t* m);

#endif