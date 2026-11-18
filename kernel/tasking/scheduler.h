#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include "process.h"

void      scheduler_init(void);
void      scheduler_start(void);
uint64_t  scheduler_tick(uint64_t old_rsp);
void      scheduler_add(Process* p);
void      scheduler_remove(Process* p);
void      scheduler_exit_current(void);
void      scheduler_yield(void);
Process*  scheduler_pick_next(void);

#endif