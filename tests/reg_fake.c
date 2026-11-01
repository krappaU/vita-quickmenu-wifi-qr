// SPDX-License-Identifier: MIT
// An in-memory registry for the host tests.
#include "reg_fake.h"

#include <stdio.h>
#include <string.h>

#include "../src/reg.h"

#define MAX_KEYS 2048
static struct { char path[96]; int is_str; int i; char s[256]; } keys[MAX_KEYS];
static int nkeys;
int reg_fake_writes, reg_fake_fail_on_write = -1;

void reg_fake_reset(void) { nkeys = 0; reg_fake_writes = 0; reg_fake_fail_on_write = -1; }

static int find(const char *c, const char *n, int create) {
    char p[96];
    snprintf(p, sizeof p, "%s/%s", c, n);
    for (int i = 0; i < nkeys; i++) if (!strcmp(keys[i].path, p)) return i;
    if (!create || nkeys == MAX_KEYS) return -1;
    memset(&keys[nkeys], 0, sizeof keys[0]);
    strcpy(keys[nkeys].path, p);
    return nkeys++;
}

int reg_get_int(const char *c, const char *n, int *out) {
    int i = find(c, n, 0);
    *out = i < 0 ? 0 : keys[i].i;
    return 0; // like the real registry: every key of all 30 slots exists, defaulting to 0 / ""
}
int reg_get_str(const char *c, const char *n, char *buf, int size) {
    int i = find(c, n, 0);
    memset(buf, 0, size);
    if (i >= 0) strncpy(buf, keys[i].s, size - 1);
    return 0;
}
static int write_ok(void) { return reg_fake_fail_on_write < 0 || reg_fake_writes++ != reg_fake_fail_on_write; }
int reg_set_int(const char *c, const char *n, int v) {
    if (!write_ok()) return (int)0x800D0001;
    keys[find(c, n, 1)].i = v;
    return 0;
}
int reg_set_str(const char *c, const char *n, const char *v, int size) {
    if (!write_ok()) return (int)0x800D0001;
    int i = find(c, n, 1);
    memset(keys[i].s, 0, sizeof keys[i].s);
    strncpy(keys[i].s, v, size - 1);
    return 0;
}
