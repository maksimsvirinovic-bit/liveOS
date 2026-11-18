#ifndef PIT_H
#define PIT_H

#include <stdint.h>

#define PIT_BASE_FREQ  1193182

#define PIT_TARGET_HZ  1000

void     pit_init(void);
uint32_t pit_get_tick(void);
void     pit_sleep_ticks(uint32_t ticks);

#endif