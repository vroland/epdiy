#include <stdio.h>
#include <unity.h>
#include "unity_test_runner.h"

static void print_banner(const char* text)
{
    printf("\n#### %s #####\n\n", text);
}


void app_main(void)
{
    print_banner("Running all the registered tests");
    UNITY_BEGIN();
    //unity_run_tests_by_tag("unit", false);
    unity_run_all_tests();
    UNITY_END();
}

