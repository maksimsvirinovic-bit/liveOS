#include "user_abi.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include <serial/serial.h>
#include <stddef.h>

static uint64_t g_abi_phys = 0;
static UserABI* g_abi = NULL;

int user_abi_init(void) {
    g_abi_phys = pmm_alloc_page();
    if (!g_abi_phys) {
        serial_emergency_print("user_abi: no phys page\n");
        return -1;
    }

    g_abi = (UserABI*)phys_to_virt(g_abi_phys);

    uint8_t* p = (uint8_t*)g_abi;
    for (uint64_t i = 0; i < PAGE_SIZE; i++) p[i] = 0;

    g_abi->magic       = USER_ABI_MAGIC;
    g_abi->version     = USER_ABI_VERSION;
    g_abi->page_size   = (uint32_t)PAGE_SIZE;
    g_abi->syscall_count = 0;
    g_abi->syscall_table = 0;

    serial_emergency_print("user_abi: phys=");
    serial_emergency_print_hex(g_abi_phys);
    serial_emergency_print(" virt=");
    serial_emergency_print_hex(USER_ABI_VADDR);
    serial_emergency_print("\n");

    return 0;
}

uint64_t user_abi_phys(void) { return g_abi_phys; }