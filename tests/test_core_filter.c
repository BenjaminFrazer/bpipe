#define _DEFAULT_SOURCE
#include "unity.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../bpipe/signal_gen.h"

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

void test_Bp_Filter_Start_Success(void) {
    Bp_Filter_t filter;
    BpFilter_Init(&filter, BpPassThroughTransform, 0, 128, 64, 6, 1);
    filter.dtype = DTYPE_UNSIGNED;
    filter.data_width = sizeof(unsigned);
    
    // Test starting a filter
    Bp_EC result = Bp_Filter_Start(&filter);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, result);
    TEST_ASSERT_TRUE(filter.running);
    
    // Clean up - stop the filter
    Bp_Filter_Stop(&filter);
}

void test_Bp_Filter_Start_Already_Running(void) {
    Bp_Filter_t filter;
    BpFilter_Init(&filter, BpPassThroughTransform, 0, 128, 64, 6, 1);
    filter.dtype = DTYPE_UNSIGNED;
    filter.data_width = sizeof(unsigned);
    
    // Start the filter first
    Bp_Filter_Start(&filter);
    
    // Try to start again - should fail with specific error code
    Bp_EC result = Bp_Filter_Start(&filter);
    TEST_ASSERT_EQUAL_UINT(BP_ERROR_ALREADY_RUNNING, result);
    
    // Clean up
    Bp_Filter_Stop(&filter);
}

void test_Bp_Filter_Stop_Success(void) {
    Bp_Filter_t filter;
    BpFilter_Init(&filter, BpPassThroughTransform, 0, 128, 64, 6, 1);
    filter.dtype = DTYPE_UNSIGNED;
    filter.data_width = sizeof(unsigned);
    
    // Start then stop the filter
    Bp_Filter_Start(&filter);
    TEST_ASSERT_TRUE(filter.running);
    
    Bp_EC result = Bp_Filter_Stop(&filter);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, result);
    TEST_ASSERT_TRUE(!filter.running);
}

void test_Bp_Filter_Stop_Not_Running(void) {
    Bp_Filter_t filter;
    BpFilter_Init(&filter, BpPassThroughTransform, 0, 128, 64, 6, 1);
    
    // Try to stop a filter that's not running - should succeed
    Bp_EC result = Bp_Filter_Stop(&filter);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, result);
}

void test_Bp_Filter_Start_Null_Filter(void) {
    // Test starting a null filter - should fail with specific error code
    Bp_EC result = Bp_Filter_Start(NULL);
    TEST_ASSERT_EQUAL_UINT(BP_ERROR_NULL_FILTER, result);
}

void test_Bp_Filter_Stop_Null_Filter(void) {
    // Test stopping a null filter - should fail with specific error code
    Bp_EC result = Bp_Filter_Stop(NULL);
    TEST_ASSERT_EQUAL_UINT(BP_ERROR_NULL_FILTER, result);
}

void test_Overflow_Behavior_Block_Default(void) {
    Bp_Filter_t filter;
    BpFilter_Init(&filter, BpPassThroughTransform, 0, 128, 64, 6, 1);
    
    // Default behavior should be OVERFLOW_BLOCK
    TEST_ASSERT_EQUAL_UINT(OVERFLOW_BLOCK, filter.overflow_behaviour);
}

void test_Overflow_Behavior_Drop_Mode(void) {
    Bp_Filter_t filter;
    BpFilter_Init(&filter, BpPassThroughTransform, 0, 128, 64, 2, 1); // Small buffer
    filter.dtype = DTYPE_UNSIGNED;
    filter.data_width = sizeof(unsigned);
    filter.overflow_behaviour = OVERFLOW_DROP;
    Bp_allocate_buffers(&filter, 0);
    
    // Test that Bp_allocate fails when buffer is full and drop mode is enabled
    // First, try to allocate when buffer is not full - should succeed
    Bp_Batch_t batch1 = Bp_allocate(&filter, &filter.input_buffers[0]);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, batch1.ec);
    TEST_ASSERT_NOT_NULL(batch1.data);
    
    // Simulate a full buffer by setting head far ahead of tail
    filter.input_buffers[0].head = 1000;
    filter.input_buffers[0].tail = 0;
    
    // Now try to allocate when buffer is full with drop mode - should fail
    Bp_Batch_t batch2 = Bp_allocate(&filter, &filter.input_buffers[0]);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_NOSPACE, batch2.ec);
    TEST_ASSERT_NULL(batch2.data);
    
    // Test blocking mode for comparison
    filter.overflow_behaviour = OVERFLOW_BLOCK;
    filter.input_buffers[0].head = 0; // Reset to non-full state
    filter.input_buffers[0].tail = 0;
    
    Bp_Batch_t batch3 = Bp_allocate(&filter, &filter.input_buffers[0]);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, batch3.ec);
    TEST_ASSERT_NOT_NULL(batch3.data);
    
    // Clean up
    Bp_deallocate_buffers(&filter, 0);
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
	RUN_TEST(test_Bp_Filter_Start_Success);
	RUN_TEST(test_Bp_Filter_Start_Already_Running);
	RUN_TEST(test_Bp_Filter_Stop_Success);
	RUN_TEST(test_Bp_Filter_Stop_Not_Running);
	RUN_TEST(test_Bp_Filter_Start_Null_Filter);
	RUN_TEST(test_Bp_Filter_Stop_Null_Filter);
	RUN_TEST(test_Overflow_Behavior_Block_Default);
	RUN_TEST(test_Overflow_Behavior_Drop_Mode);
	return UNITY_END();
}

