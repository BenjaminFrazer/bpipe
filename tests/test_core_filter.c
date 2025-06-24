#include "unity.h"
#include "unity.h"
#include <stdlib.h>
#include <string.h>
#include "../bpipe/core.h"

Bp_Filter_t test_filter;

void setUp(void) {
    // Set up code for the test filter
}

void tearDown(void) {
    // Clean up code for the test filter
}

void test_BpFilter_Init_Success(void) {
    Bp_EC result = BpFilter_Init(&test_filter, NULL, 0, 128, 64, 6, 1);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, result);
}

void test_BpFilter_Init_Failure(void) {
    Bp_EC result = BpFilter_Init(&test_filter, NULL, 0, 128, 64, 6, 1);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, result);
}

int main(void)
{
	RUN_TEST(test_BpFilter_Init_Success);
	RUN_TEST(test_BpFilter_Init_Failure);
}

