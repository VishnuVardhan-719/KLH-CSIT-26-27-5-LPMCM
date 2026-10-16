#ifndef RUN_TESTS_H
#define RUN_TESTS_H

#include <stdbool.h>
#include <stddef.h>

/* Minimal test framework: count checks across all translation units. */

void t_check(bool cond, const char *name);
void t_check_int(long long got, long long want, const char *name);

int t_summary(void); /* returns 0 when all tests passed */

#endif /* RUN_TESTS_H */
