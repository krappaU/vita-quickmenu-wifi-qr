// SPDX-License-Identifier: MIT
#ifndef LIBC_SHIM_H
#define LIBC_SHIM_H

#include <stddef.h>

// The heap for one scan; everything malloc'd in between is released by arena_close().
int arena_open(size_t size);
void arena_close(void);

#endif
