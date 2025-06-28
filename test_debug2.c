#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../bpipe/core.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_drop_mode_allocate(void)
{
    printf("Starting drop mode test\n");
    Bp_Filter_t filter;
    BpFilter_Init(&filter, BpPassThroughTransform, 0, 128, 64, 2, 1);
    filter.dtype = DTYPE_UNSIGNED;
    filter.data_width = sizeof(unsigned);
    filter.overflow_behaviour = OVERFLOW_DROP;
    
    printf("Allocating buffers\n");
    Bp_allocate_buffers(&filter, 0);
    
    printf("Setting buffer to appear full\n");
    filter.input_buffers[0].head = 1000;
    filter.input_buffers[0].tail = 0;
    
    printf("Trying allocate with drop mode on full buffer\n");
    Bp_Batch_t batch = Bp_allocate(&filter, &filter.input_buffers[0]);
    printf("Allocate returned with ec=%d\n", batch.ec);
    
    TEST_ASSERT_EQUAL_UINT(Bp_EC_NOSPACE, batch.ec);
    
    printf("Cleaning up\n");
    Bp_deallocate_buffers(&filter, 0);
    printf("Test completed\n");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_drop_mode_allocate);
    return UNITY_END();
}