#include "mutex.h"
#include "tasking/scheduler.h"
#include "heap.h"

void mutex_lock(mutex_t* m) {
    for (;;) {
        if (!__atomic_test_and_set(&m->locked, __ATOMIC_ACQUIRE)) {
            m->owner = process_get_current();
            return;
        }

        /* Медленный путь: занят. Добавляем себя в waiters и спим. */
        Process* current = process_get_current();
        if (!current) return;

        WaitNode* node = (WaitNode*)kmalloc(sizeof(WaitNode));
        if (!node) {
            /* OOM — fallback: спин до освобождения (плохо, но не зависнем) */
            while (__atomic_load_n(&m->locked, __ATOMIC_RELAXED)) {
                __asm__ volatile("pause" ::: "memory");
            }
            continue;
        }
        node->p = current;
        node->next = m->waiters;
        m->waiters = node;

        scheduler_sleep();

    }
}

void mutex_unlock(mutex_t* m) {
    m->owner = 0;
    __atomic_clear(&m->locked, __ATOMIC_RELEASE);

    WaitNode* node = m->waiters;
    m->waiters = NULL;
    while (node) {
        WaitNode* next = node->next;
        if (node->p->state == PROC_SLEEPING) {
            scheduler_wakeup(node->p);
        }
        kfree(node);
        node = next;
    }
}