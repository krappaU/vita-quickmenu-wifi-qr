// SPDX-License-Identifier: MIT
#ifndef CHECK_H
#define CHECK_H
#include <stdio.h>
#include <string.h>
static int failures, checks;
#define CHECK_TRUE(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_INT(a, b) do { long long a_ = (a), b_ = (b); checks++; if (a_ != b_) { failures++; printf("FAIL %s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)
#define CHECK_STR(a, b) do { const char *a_ = (a), *b_ = (b); checks++; if (strcmp(a_, b_)) { failures++; printf("FAIL %s:%d: %s == \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)
#define CHECK_DONE(name) (printf("%s: %d checks, %d failed\n", name, checks, failures), failures != 0)
#endif
