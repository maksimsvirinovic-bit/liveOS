#ifndef SYMBOL_H
#define SYMBOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define SYM_NAME_MAX  64
#define SYM_HASH_SIZE 256

#define SYM_KERNEL    (1 << 0)
#define SYM_USER      (1 << 1)
#define SYM_FUNC      (1 << 2)
#define SYM_DATA      (1 << 3)
#define SYM_EXPORTED  (1 << 4)
#define SYM_WEAK      (1 << 5)

typedef struct Symbol {
    char     name[SYM_NAME_MAX];
    uint64_t addr;
    uint64_t flags;
    uint32_t version;
    struct Symbol* next;
} Symbol;

void     symbol_init(void);
int      symbol_register(const char* name, uint64_t addr, uint64_t flags, uint32_t version);
int      symbol_unregister(const char* name);
uint64_t symbol_resolve(const char* name);
Symbol*  symbol_lookup(const char* name);
void     symbol_list(void);

#ifdef __cplusplus
}
#endif

#endif