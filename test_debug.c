#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../bpipe/core.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_simple_init(void)
{
    printf("Starting simple init test\n");
    Bp_Filter_t filter;
    Bp_EC result = BpFilter_Init(&filter, BpPassThroughTransform, 0, 128, 64, 6, 1);
    printf("Init result: %d\n", result);
    TEST_ASSERT_EQUAL(Bp_EC_OK, result);
    printf("Test passed\n");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_simple_init);
    return UNITY_END();
}