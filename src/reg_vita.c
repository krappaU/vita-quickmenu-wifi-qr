// SPDX-License-Identifier: MIT
#include "reg.h"

#include <psp2/kernel/clib.h>
#include <psp2/registrymgr.h>

int reg_get_int(const char *category, const char *name, int *out) {
    return sceRegMgrGetKeyInt(category, name, out);
}

int reg_get_str(const char *category, const char *name, char *buf, int size) {
    sceClibMemset(buf, 0, size);
    int rc = sceRegMgrGetKeyStr(category, name, buf, size);
    buf[size - 1] = 0;
    return rc;
}

int reg_set_int(const char *category, const char *name, int value) {
    return sceRegMgrSetKeyInt(category, name, value);
}

int reg_set_str(const char *category, const char *name, const char *value, int size) {
    // The registry stores fixed-size string fields: pass the whole field, zero-padded,
    // as Vita Account Manager and Registry Editor do.
    char buf[256];
    if (size > (int)sizeof buf) return -1;
    sceClibMemset(buf, 0, sizeof buf);
    sceClibStrncpy(buf, value, size - 1);
    return sceRegMgrSetKeyStr(category, name, buf, size);
}
