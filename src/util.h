// SPDX-License-Identifier: MIT
#ifndef UTIL_H
#define UTIL_H

#ifdef __vita__
#include <psp2/kernel/clib.h>
#define u_snprintf sceClibSnprintf
#define u_vsnprintf sceClibVsnprintf
#else
#include <stdio.h>
#define u_snprintf snprintf
#define u_vsnprintf vsnprintf
#endif

#endif
