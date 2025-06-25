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

void test_Bp_add_sink_Success(void) {
    Bp_Filter_t filter1, filter2;
    BpFilter_Init(&filter1, BpPassThroughTransform, 0, 128, 64, 6, 1);
    BpFilter_Init(&filter2, BpPassThroughTransform, 0, 128, 64, 6, 1);
    
    Bp_EC result = Bp_add_sink(&filter1, &filter2);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, result);
    TEST_ASSERT_EQUAL_UINT(1, filter1.n_sinks);
    TEST_ASSERT_TRUE(filter1.sinks[0] == &filter2);
}

void test_Bp_add_multiple_sinks(void) {
    Bp_Filter_t filter1, filter2, filter3;
    BpFilter_Init(&filter1, BpPassThroughTransform, 0, 128, 64, 6, 1);
    BpFilter_Init(&filter2, BpPassThroughTransform, 0, 128, 64, 6, 1);
    BpFilter_Init(&filter3, BpPassThroughTransform, 0, 128, 64, 6, 1);
    
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, Bp_add_sink(&filter1, &filter2));
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, Bp_add_sink(&filter1, &filter3));
    
    TEST_ASSERT_EQUAL_UINT(2, filter1.n_sinks);
    TEST_ASSERT_TRUE(filter1.sinks[0] == &filter2);
    TEST_ASSERT_TRUE(filter1.sinks[1] == &filter3);
}

void test_Bp_remove_sink_Success(void) {
    Bp_Filter_t filter1, filter2, filter3;
    BpFilter_Init(&filter1, BpPassThroughTransform, 0, 128, 64, 6, 1);
    BpFilter_Init(&filter2, BpPassThroughTransform, 0, 128, 64, 6, 1);
    BpFilter_Init(&filter3, BpPassThroughTransform, 0, 128, 64, 6, 1);
    
    Bp_add_sink(&filter1, &filter2);
    Bp_add_sink(&filter1, &filter3);
    
    Bp_EC result = Bp_remove_sink(&filter1, &filter2);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, result);
    TEST_ASSERT_EQUAL_UINT(1, filter1.n_sinks);
    TEST_ASSERT_TRUE(filter1.sinks[0] == &filter3);
}

void test_multi_transform_function(void) {
    Bp_Filter_t filter;
    BpFilter_Init(&filter, BpPassThroughTransform, 0, 128, 64, 6, 1);
    
    // Test that transform is set correctly
    TEST_ASSERT_TRUE(filter.transform != NULL);
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_BpFilter_Init_Success);
	RUN_TEST(test_BpFilter_Init_Failure);
	RUN_TEST(test_Bp_add_sink_Success);
	RUN_TEST(test_Bp_add_multiple_sinks);
	RUN_TEST(test_Bp_remove_sink_Success);
	RUN_TEST(test_multi_transform_function);
	return UNITY_END();
}

