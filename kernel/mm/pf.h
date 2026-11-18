#ifndef PF_H
#define PF_H

#include <x86_64/interrupts/exceptions.h>

void pf_init(void);
ExceptionResult pf_handler(ExceptionContext* ctx, void* user);

#endif