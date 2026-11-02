// SPDX-License-Identifier: MIT
// The C library functions quirc and the compiler need. A plugin in SceShell has no newlib, so
// memory comes from one memory block per scan (arena_open .. arena_close) and the rest maps to
// SceLibKernel's sceClib functions.
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <stddef.h>
#include <stdint.h>

#include "libc_shim.h"

static SceUID arena_uid = -1;
static uint8_t *arena_base;
static size_t arena_size, arena_used;

int arena_open(size_t size) {
    size = (size + 0xFFFFF) & ~(size_t)0xFFFFF;
    arena_uid = sceKernelAllocMemBlock("wifiqr_heap", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, size, NULL);
    if (arena_uid < 0) return arena_uid;
    void *base;
    sceKernelGetMemBlockBase(arena_uid, &base);
    arena_base = base;
    arena_size = size;
    arena_used = 0;
    return 0;
}

void arena_close(void) {
    if (arena_uid >= 0) sceKernelFreeMemBlock(arena_uid);
    arena_uid = -1;
    arena_base = NULL;
    arena_size = arena_used = 0;
}

// A bump allocator: quirc makes a handful of allocations per scan and frees them all at the end,
// so free() does nothing and arena_close() returns everything at once.
typedef struct { size_t size; size_t pad; } hdr;

void *malloc(size_t n) {
    if (!arena_base) return NULL;
    size_t need = (sizeof(hdr) + n + 15) & ~(size_t)15;
    if (arena_used + need > arena_size) return NULL;
    hdr *h = (hdr *)(arena_base + arena_used);
    h->size = n;
    arena_used += need;
    return h + 1;
}

void free(void *p) { (void)p; }

void *calloc(size_t a, size_t b) {
    if (b && a > SIZE_MAX / b) return NULL;
    void *p = malloc(a * b);
    if (p) sceClibMemset(p, 0, a * b);
    return p;
}

void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    hdr *h = (hdr *)p - 1;
    if (n <= h->size) return p;
    void *q = malloc(n);
    if (q) sceClibMemcpy(q, p, h->size);
    return q;
}

void *memset(void *d, int c, size_t n) { return sceClibMemset(d, c, n); }
void *memcpy(void *d, const void *s, size_t n) { return sceClibMemcpy(d, s, n); }
void *memmove(void *d, const void *s, size_t n) { return sceClibMemmove(d, s, n); }
int memcmp(const void *a, const void *b, size_t n) { return sceClibMemcmp(a, b, n); }
void *memchr(const void *s, int c, size_t n) { return sceClibMemchr(s, c, n); }
size_t strlen(const char *s) { return sceClibStrnlen(s, 0x7fffffff); }
int strcmp(const char *a, const char *b) { return sceClibStrcmp(a, b); }

// quirc's geometry (built with float, so the f versions; the double ones for completeness).
// Inline VFP instructions: a call to __builtin_sqrt may compile back into a call to sqrt itself.
float sqrtf(float x) { float r; __asm__("vsqrt.f32 %0, %1" : "=t"(r) : "t"(x)); return r; }
double sqrt(double x) { double r; __asm__("vsqrt.f64 %P0, %P1" : "=w"(r) : "w"(x)); return r; }
float fabsf(float x) { return x < 0 ? -x : x; }
double fabs(double x) { return x < 0 ? -x : x; }
// rint/rintf round to the nearest integer, ties to even (the default rounding mode): one VFP
// instruction, vcvtr, does exactly that. quirc's grid fitting calls rintf ~2 million times per
// frame with a code in view; the first version, through a 64-bit integer conversion that the
// Cortex-A9 does in software, made those frames take ~0.7 s on the console (log of 2026-11-02).
// Pixel coordinates are well inside the int range vcvtr saturates to.
float rintf(float x) {
    float r;
    __asm__("vcvtr.s32.f32 %0, %1\n\tvcvt.f32.s32 %0, %0" : "=t"(r) : "t"(x));
    return r;
}
double rint(double x) {
    float tmp;
    double r;
    __asm__("vcvtr.s32.f64 %0, %P1" : "=t"(tmp) : "w"(x));
    __asm__("vcvt.f64.s32 %P0, %1" : "=w"(r) : "t"(tmp));
    return r;
}
int abs(int x) { return x < 0 ? -x : x; }
