// SPDX-License-Identifier: MIT
// The few registry calls profiles.c needs. reg_vita.c maps them to sceRegMgr*; the host tests
// link an in-memory fake instead.
#ifndef REG_H
#define REG_H

int reg_get_int(const char *category, const char *name, int *out);
int reg_get_str(const char *category, const char *name, char *buf, int size);
int reg_set_int(const char *category, const char *name, int value);
int reg_set_str(const char *category, const char *name, const char *value, int size);

#endif
