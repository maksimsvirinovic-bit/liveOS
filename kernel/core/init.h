#ifndef INIT_H
#define INIT_H

#include <stdint.h>
#include "tasking/process.h"

Process* init_load_program(const char* path, int pid, const char* name);

#endif