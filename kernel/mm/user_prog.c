#include "user_prog.h"
#include "vmm.h"
#include <stddef.h>

void user_prog_build(uint8_t* buf, uint64_t base_addr) {
    uint64_t msg_addr = base_addr + 46;

    uint8_t code[] = {
        0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,
        0x48, 0xC7, 0xC7, 0x01, 0x00, 0x00, 0x00,
        0x48, 0xBE,
        (uint8_t)(msg_addr),
        (uint8_t)(msg_addr >> 8),
        (uint8_t)(msg_addr >> 16),
        (uint8_t)(msg_addr >> 24),
        (uint8_t)(msg_addr >> 32),
        (uint8_t)(msg_addr >> 40),
        (uint8_t)(msg_addr >> 48),
        (uint8_t)(msg_addr >> 56),
        0x48, 0xC7, 0xC2, 0x0E, 0x00, 0x00, 0x00,
        0x0F, 0x05,
        0x48, 0xC7, 0xC0, 0x3C, 0x00, 0x00, 0x00,
        0x48, 0x31, 0xFF,
        0x0F, 0x05,
        0xF4,
    };
    for (uint64_t i = 0; i < sizeof(code); i++) buf[i] = code[i];

    const char* msg = "Hello, user!\n\0";
    for (uint64_t i = 0; i < 15; i++) buf[46 + i] = (uint8_t)msg[i];

}