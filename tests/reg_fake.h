// SPDX-License-Identifier: MIT
#ifndef REG_FAKE_H
#define REG_FAKE_H
void reg_fake_reset(void);
extern int reg_fake_writes, reg_fake_fail_on_write; // fail the Nth write (0-based); -1: never
#endif
