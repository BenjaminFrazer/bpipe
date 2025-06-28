#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../bpipe/core.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_filter_start_stop(void)
{
    printf("Creating filter with no inputs\n");
    Bp_Filter_t filter;
    BpFilter_Init(&filter, BpPassThroughTransform, 0, 128, 64, 6, 0);
    filter.dtype = DTYPE_UNSIGNED;
    filter.data_width = sizeof(unsigned);
    
    printf("Starting filter\n");
    Bp_EC result = Bp_Filter_Start(&filter);
    printf("Start result: %d\n", result);
    TEST_ASSERT_EQUAL(Bp_EC_OK, result);
    
    printf("Filter running: %d\n", filter.running);
    
    printf("Stopping filter\n");
    result = Bp_Filter_Stop(&filter);
    printf("Stop result: %d\n", result);
    
    printf("Test completed\n");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_filter_start_stop);
    return UNITY_END();
}