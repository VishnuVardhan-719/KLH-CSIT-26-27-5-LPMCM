#include "run_tests.h"

#include <stdio.h>

void run_parsing_tests(void);
void run_tree_tests(void);
void run_live_tests(void);

int main(void)
{
    printf("=== Linux Process Monitor: test suite ===\n\n");

    run_parsing_tests();
    printf("\n");
    run_tree_tests();
    printf("\n");
    run_live_tests();

    return t_summary();
}
