#include "symbol.h"
#include "heap.h"
#include "serial/serial.h"
#include <stddef.h>
#include <str.h>

static Symbol* g_buckets[SYM_HASH_SIZE];

void symbol_init(void) {
    for (int i = 0; i < SYM_HASH_SIZE; i++) g_buckets[i] = NULL;
}

static uint32_t sym_hash(const char* name) {
    uint32_t h = 5381;
    while (*name) h = h * 33 + (uint8_t)*name++;
    return h % SYM_HASH_SIZE;
}

int symbol_register(const char* name, uint64_t addr, uint64_t flags, uint32_t version) {
    if (!name || !*name) return -1;

    uint32_t b = sym_hash(name);

    for (Symbol* s = g_buckets[b]; s; s = s->next) {
        if (strcmp(s->name, name) == 0) {
            if (s->flags & SYM_WEAK) {
                s->addr = addr;
                s->flags = flags;
                s->version = version;
                return 0;
            }
            if (flags & SYM_WEAK) return 0;
            return -1;
        }
    }

    Symbol* s = (Symbol*)kmalloc(sizeof(Symbol));
    if (!s) return -1;
    strncpy(s->name, name, SYM_NAME_MAX - 1);
    s->name[SYM_NAME_MAX - 1] = '\0';
    s->addr = addr;
    s->flags = flags;
    s->version = version;
    s->next = g_buckets[b];
    g_buckets[b] = s;
    return 0;
}

int symbol_unregister(const char* name) {
    uint32_t b = sym_hash(name);
    Symbol** pp = &g_buckets[b];
    while (*pp) {
        if (strcmp((*pp)->name, name) == 0) {
            Symbol* dead = *pp;
            *pp = dead->next;
            kfree(dead);
            return 0;
        }
        pp = &(*pp)->next;
    }
    return -1;
}

Symbol* symbol_lookup(const char* name) {
    uint32_t b = sym_hash(name);
    for (Symbol* s = g_buckets[b]; s; s = s->next) {
        if (strcmp(s->name, name) == 0) return s;
    }
    return NULL;
}

uint64_t symbol_resolve(const char* name) {
    Symbol* s = symbol_lookup(name);
    return s ? s->addr : 0;
}

void symbol_list(void) {
    serial_emergency_print("Symbols:\n");
    for (int i = 0; i < SYM_HASH_SIZE; i++) {
        for (Symbol* s = g_buckets[i]; s; s = s->next) {
            serial_emergency_print("  ");
            serial_emergency_print(s->name);
            serial_emergency_print(" = ");
            serial_emergency_print_hex(s->addr);
            serial_emergency_print("\n");
        }
    }
}