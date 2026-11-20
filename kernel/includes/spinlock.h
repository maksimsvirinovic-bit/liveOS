#ifndef KERNEL_SPINLOCK_H
#define KERNEL_SPINLOCK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    volatile uint8_t locked;
    uint64_t         flags;
} spinlock_t;

#define SPINLOCK_INIT { 0, 0 }

static inline void spinlock_init(spinlock_t *lock) {
    lock->locked = 0;
    lock->flags  = 0;
}

static inline void spin_lock(spinlock_t *lock) {
    while (__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&lock->locked, __ATOMIC_RELAXED)) {
            __asm__ volatile("pause" ::: "memory");
        }
    }
}

static inline bool spin_trylock(spinlock_t *lock) {
    return !__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE);
}

static inline void spin_unlock(spinlock_t *lock) {
    __atomic_clear(&lock->locked, __ATOMIC_RELEASE);
}

static inline bool spin_is_locked(const spinlock_t *lock) {
    return __atomic_load_n(&lock->locked, __ATOMIC_RELAXED) != 0;
}

static inline void spin_lock_irqsave(spinlock_t *lock) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    spin_lock(lock);
    lock->flags = flags;
}

static inline void spin_unlock_irqrestore(spinlock_t *lock) {
    uint64_t flags = lock->flags;
    spin_unlock(lock);
    if (flags & (1ULL << 9)) {
        __asm__ volatile("sti" ::: "memory");
    }
}

#ifdef __cplusplus
}
#endif

#endif /* KERNEL_SPINLOCK_H */
