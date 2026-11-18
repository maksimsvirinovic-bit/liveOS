#include <stddef.h>
#include <heap.h>
#include <panic.h>

void* operator new(size_t n) {
    void* p = kmalloc(n);
    if (!p) panic("operator new: OOM (%lu bytes)", (unsigned long)n);
    return p;
}

void* operator new[](size_t n) {
    void* p = kmalloc(n);
    if (!p) panic("operator new[]: OOM (%lu bytes)", (unsigned long)n);
    return p;
}

void operator delete(void* p) noexcept              { if (p) kfree(p); }
void operator delete[](void* p) noexcept            { if (p) kfree(p); }
void operator delete(void* p, size_t) noexcept      { if (p) kfree(p); }
void operator delete[](void* p, size_t) noexcept    { if (p) kfree(p); }


extern "C" void __cxa_pure_virtual() {
    panic("__cxa_pure_virtual called");
}

extern "C" int __cxa_atexit(void (*)(void*), void*, void*) {
    return 0;
}

extern "C" void __cxa_guard_acquire() {}
extern "C" void __cxa_guard_release() {}

extern "C" {
    extern void (*__init_array_start)();
    extern void (*__init_array_end)();
}

extern "C" void run_global_ctors() {
    void (**p)() = &__init_array_start;
    void (**end)() = &__init_array_end;
    while (p < end) {
        (*p)();
        p++;
    }
}