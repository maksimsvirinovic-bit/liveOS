#ifndef PATH_H
#define PATH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
int path_normalize(const char* cwd, const char* input, char* out, uint32_t out_size);

#ifdef __cplusplus
}
#endif

#endif