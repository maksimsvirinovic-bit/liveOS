#include "efi_rt.h"
#include <boot_info.h>
#include <serial/serial.h>
#include <stddef.h>

#define EFI_SUCCESS  0

static EfiRuntimeServices* g_rts = NULL;

int efi_rt_init(void) {
    BOOT_INFO* bi = (BOOT_INFO*)0x5000;
    if (!bi->RuntimeServices) return -1;
    serial_emergency_print("efi_rt: RTS @");
    serial_emergency_print_hex(bi->RuntimeServices);
    serial_emergency_print("\n");
    serial_emergency_print("efi_rt: GetTime field = ");
    serial_emergency_print_hex((uint64_t)g_rts->GetTime);
    serial_emergency_print("\n");
    g_rts = (EfiRuntimeServices*)bi->RuntimeServices;
    if (!g_rts->GetTime) return -1;
    EfiTime t;
    typedef uint64_t (*GetTimeFn)(EfiTime*, void*);
    GetTimeFn fn = (GetTimeFn)g_rts->GetTime;
    if (fn(&t, NULL) != 0) return -1;
    return 0;
}

int efi_rt_available(void) {
    return g_rts != NULL;
}

int efi_rt_get_time(EfiTime* out) {
    if (!g_rts || !out) return -1;
    typedef uint64_t (*GetTimeFn)(EfiTime*, void*);
    GetTimeFn fn = (GetTimeFn)g_rts->GetTime;
    uint64_t status = fn(out, NULL);
    return (status == 0) ? 0 : -1;
}