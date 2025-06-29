#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include "../bpipe/signal_gen.h"
#include "unity.h"

Bp_Filter_t test_filter;

void setUp(void)
{
    // Set up code for the test filter
}

void tearDown(void)
{
    // Enhanced cleanup to prevent resource leaks between tests
    // This ensures all filter resources are properly cleaned up
    BpFilter_Deinit(&test_filter);
    
    // Reset global test filter to zero state for next test
    memset(&test_filter, 0, sizeof(test_filter));
}

void test_BpFilter_Init_Success(void)
{
    BpFilterConfig config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_FLOAT,
        .buffer_size = 128,
        .batch_size = 64,
        .number_of_batches_exponent = 6,
        .number_of_input_filters = 1
    };
    Bp_EC result = BpFilter_Init(&test_filter, &config);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, result);
}

void test_BpFilter_Init_Failure(void)
{
    Bp_EC result = BpFilter_Init(&test_filter, NULL);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_CONFIG_REQUIRED, result);
}

void test_Bp_add_sink_Success(void)
{
    Bp_Filter_t filter1, filter2;
    BpFilterConfig config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_FLOAT,
        .buffer_size = 128,
        .batch_size = 64,
        .number_of_batches_exponent = 6,
        .number_of_input_filters = 1
    };
    BpFilter_Init(&filter1, &config);
    BpFilter_Init(&filter2, &config);

    Bp_EC result = Bp_add_sink(&filter1, &filter2);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, result);
    TEST_ASSERT_EQUAL_UINT(1, filter1.n_sinks);
    TEST_ASSERT_TRUE(filter1.sinks[0] == &filter2);
    
    // Clean up local filters to prevent resource leaks
    BpFilter_Deinit(&filter1);
    BpFilter_Deinit(&filter2);
}

void test_Bp_add_multiple_sinks(void)
{
    Bp_Filter_t filter1, filter2, filter3;
    BpFilterConfig config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_FLOAT,
        .buffer_size = 128,
        .batch_size = 64,
        .number_of_batches_exponent = 6,
        .number_of_input_filters = 1
    };
    BpFilter_Init(&filter1, &config);
    BpFilter_Init(&filter2, &config);
    BpFilter_Init(&filter3, &config);

    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, Bp_add_sink(&filter1, &filter2));
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, Bp_add_sink(&filter1, &filter3));

    TEST_ASSERT_EQUAL_UINT(2, filter1.n_sinks);
    TEST_ASSERT_TRUE(filter1.sinks[0] == &filter2);
    TEST_ASSERT_TRUE(filter1.sinks[1] == &filter3);
    
    // Clean up local filters to prevent resource leaks
    BpFilter_Deinit(&filter1);
    BpFilter_Deinit(&filter2);
    BpFilter_Deinit(&filter3);
}

void test_Bp_remove_sink_Success(void)
{
    Bp_Filter_t filter1, filter2, filter3;
    BpFilterConfig config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_FLOAT,
        .buffer_size = 128,
        .batch_size = 64,
        .number_of_batches_exponent = 6,
        .number_of_input_filters = 1
    };
    BpFilter_Init(&filter1, &config);
    BpFilter_Init(&filter2, &config);
    BpFilter_Init(&filter3, &config);

    Bp_add_sink(&filter1, &filter2);
    Bp_add_sink(&filter1, &filter3);

    Bp_EC result = Bp_remove_sink(&filter1, &filter2);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, result);
    TEST_ASSERT_EQUAL_UINT(1, filter1.n_sinks);
    TEST_ASSERT_TRUE(filter1.sinks[0] == &filter3);
    
    // Clean up local filters to prevent resource leaks
    BpFilter_Deinit(&filter1);
    BpFilter_Deinit(&filter2);
    BpFilter_Deinit(&filter3);
}

void test_multi_transform_function(void)
{
    Bp_Filter_t filter;
    BpFilterConfig config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_FLOAT,
        .buffer_size = 128,
        .batch_size = 64,
        .number_of_batches_exponent = 6,
        .number_of_input_filters = 1
    };
    BpFilter_Init(&filter, &config);

    // Test that transform is set correctly
    TEST_ASSERT_TRUE(filter.transform != NULL);
    
    // Clean up local filter to prevent resource leaks
    BpFilter_Deinit(&filter);
}

void test_Bp_Filter_Start_Success(void)
{
    Bp_Filter_t filter;
    BpFilterConfig config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_UNSIGNED,
        .buffer_size = 128,
        .batch_size = 64,
        .number_of_batches_exponent = 6,
        .number_of_input_filters = 0
    };
    BpFilter_Init(&filter, &config);

    // Test starting a filter
    Bp_EC result = Bp_Filter_Start(&filter);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, result);
    TEST_ASSERT_TRUE(filter.running);

    // Clean up - stop and deinitialize the filter to ensure complete cleanup
    Bp_EC stop_result = Bp_Filter_Stop(&filter);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, stop_result);
    TEST_ASSERT_FALSE(filter.running);
    
    // Ensure all resources are cleaned up
    BpFilter_Deinit(&filter);
}

void test_Bp_Filter_Start_Already_Running(void)
{
    Bp_Filter_t filter;
    BpFilterConfig config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_UNSIGNED,
        .buffer_size = 128,
        .batch_size = 64,
        .number_of_batches_exponent = 6,
        .number_of_input_filters = 0
    };
    BpFilter_Init(&filter, &config);

    // Start the filter first
    Bp_EC start_result = Bp_Filter_Start(&filter);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, start_result);

    // Try to start again - should fail with specific error code
    Bp_EC result = Bp_Filter_Start(&filter);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_ALREADY_RUNNING, result);

    // Clean up - ensure proper stop and deinit sequence
    Bp_EC stop_result = Bp_Filter_Stop(&filter);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, stop_result);
    TEST_ASSERT_FALSE(filter.running);
    
    // Ensure all resources are cleaned up
    BpFilter_Deinit(&filter);
}

void test_Bp_Filter_Stop_Success(void)
{
    Bp_Filter_t filter;
    BpFilterConfig config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_UNSIGNED,
        .buffer_size = 128,
        .batch_size = 64,
        .number_of_batches_exponent = 6,
        .number_of_input_filters = 0
    };
    BpFilter_Init(&filter, &config);

    // Start then stop the filter
    Bp_EC start_result = Bp_Filter_Start(&filter);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, start_result);
    TEST_ASSERT_TRUE(filter.running);

    Bp_EC result = Bp_Filter_Stop(&filter);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, result);
    TEST_ASSERT_FALSE(filter.running);
    
    // Ensure all resources are cleaned up
    BpFilter_Deinit(&filter);
}

void test_Bp_Filter_Stop_Not_Running(void)
{
    Bp_Filter_t filter;
    BpFilterConfig config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_FLOAT,
        .buffer_size = 128,
        .batch_size = 64,
        .number_of_batches_exponent = 6,
        .number_of_input_filters = 0
    };
    BpFilter_Init(&filter, &config);

    // Try to stop a filter that's not running - should succeed
    Bp_EC result = Bp_Filter_Stop(&filter);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, result);
    
    // Ensure all resources are cleaned up
    BpFilter_Deinit(&filter);
}

void test_Bp_Filter_Start_Null_Filter(void)
{
    // Test starting a null filter - should fail with specific error code
    Bp_EC result = Bp_Filter_Start(NULL);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_NULL_FILTER, result);
}

void test_Bp_Filter_Stop_Null_Filter(void)
{
    // Test stopping a null filter - should fail with specific error code
    Bp_EC result = Bp_Filter_Stop(NULL);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_NULL_FILTER, result);
}

void test_Overflow_Behavior_Block_Default(void)
{
    Bp_Filter_t filter;
    BpFilterConfig config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_FLOAT,
        .buffer_size = 128,
        .batch_size = 64,
        .number_of_batches_exponent = 6,
        .number_of_input_filters = 1
    };
    Bp_EC init_result = BpFilter_Init(&filter, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, init_result);

    // Default behavior should be OVERFLOW_BLOCK
    TEST_ASSERT_EQUAL_UINT(OVERFLOW_BLOCK, filter.overflow_behaviour);
    
    // Clean up local filter to prevent resource leaks
    BpFilter_Deinit(&filter);
}

void test_Overflow_Behavior_Drop_Mode(void)
{
    Bp_Filter_t filter;
    
    // Use the new API to properly configure overflow behavior
    BpFilterConfig config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_UNSIGNED,
        .buffer_size = 128,
        .batch_size = 64,
        .number_of_batches_exponent = 2,  // Small ring buffer (4 batches)
        .number_of_input_filters = 1,
        .overflow_behaviour = OVERFLOW_DROP,  // Set drop mode during initialization
        .auto_allocate_buffers = true,
        .timeout_us = 1000000
    };
    
    Bp_EC init_result = BpFilter_Init(&filter, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, init_result);
    
    Bp_BatchBuffer_t* buf = &filter.input_buffers[0];
    
    // Verify the buffer got the overflow behavior setting
    TEST_ASSERT_EQUAL(OVERFLOW_DROP, buf->overflow_behaviour);
    
    // Test that Bp_allocate succeeds when buffer is not full
    Bp_Batch_t batch1 = Bp_allocate(&filter, buf);
    TEST_ASSERT_EQUAL(Bp_EC_OK, batch1.ec);
    TEST_ASSERT_NOT_NULL(batch1.data);
    
    // Simulate a full buffer by setting head far ahead of tail
    // Ring capacity is 1 << 2 = 4, so head - tail >= 4 means full
    buf->head = 1000;
    buf->tail = 0;
    
    // Verify buffer is considered full
    TEST_ASSERT_TRUE(Bp_full(buf));
    
    // Now try to allocate when buffer is full with drop mode - should fail immediately
    Bp_Batch_t batch2 = Bp_allocate(&filter, buf);
    TEST_ASSERT_EQUAL(Bp_EC_NOSPACE, batch2.ec);
    TEST_ASSERT_NULL(batch2.data);
    
    // Test that block mode would behave differently (for comparison)
    // Change buffer to blocking mode
    buf->overflow_behaviour = OVERFLOW_BLOCK;
    buf->timeout_us = 100000;  // 100ms timeout to prevent hanging
    
    // Reset to non-full state
    buf->head = 0;
    buf->tail = 0;
    
    Bp_Batch_t batch3 = Bp_allocate(&filter, buf);
    TEST_ASSERT_EQUAL(Bp_EC_OK, batch3.ec);
    TEST_ASSERT_NOT_NULL(batch3.data);
    
    // Clean up
    BpFilter_Deinit(&filter);
}

void test_Await_Timeout_Behavior(void)
{
    Bp_Filter_t filter;
    BpFilterConfig config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_FLOAT,
        .buffer_size = 128,
        .batch_size = 32,
        .number_of_batches_exponent = 6,
        .number_of_input_filters = 1
    };
    Bp_EC init_result = BpFilter_Init(&filter, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, init_result);
    
    filter.timeout.tv_sec = 0;
    filter.timeout.tv_nsec = 100000000;  // 100ms timeout
    
    Bp_BatchBuffer_t* buf = &filter.input_buffers[0];
    
    // Test timeout on empty buffer with more generous timing tolerance
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // Should timeout after 100ms since buffer is empty
    Bp_EC result = Bp_await_not_empty(buf, 100000);  // 100ms in microseconds
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    TEST_ASSERT_EQUAL(Bp_EC_TIMEOUT, result);
    
    // Verify it actually waited approximately 100ms (more generous tolerance for CI environments)
    long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
    TEST_ASSERT_TRUE(elapsed_ms >= 80 && elapsed_ms <= 200);  // More generous tolerance
    
    // Ensure complete cleanup to prevent hangs in subsequent tests
    BpFilter_Deinit(&filter);
}

void test_Await_Stopped_Behavior(void)
{
    Bp_Filter_t filter;
    BpFilterConfig config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_FLOAT,
        .buffer_size = 128,
        .batch_size = 32,
        .number_of_batches_exponent = 6,
        .number_of_input_filters = 1
    };
    Bp_EC init_result = BpFilter_Init(&filter, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, init_result);
    
    Bp_BatchBuffer_t* buf = &filter.input_buffers[0];
    
    // Stop the buffer
    BpBatchBuffer_stop(buf);
    
    // Both await functions should return STOPPED immediately
    Bp_EC result1 = Bp_await_not_empty(buf, 0);  // No timeout
    TEST_ASSERT_EQUAL(Bp_EC_STOPPED, result1);
    
    Bp_EC result2 = Bp_await_not_full(buf, 0);   // No timeout
    TEST_ASSERT_EQUAL(Bp_EC_STOPPED, result2);
    
    // Ensure complete cleanup to prevent resource leaks
    BpFilter_Deinit(&filter);
}

int main(int argc, char* argv[])
{
    UNITY_BEGIN();
    
    // Run specific test if provided as argument
    if (argc > 1) {
        const char* test_name = argv[1];
        if (strcmp(test_name, "test_BpFilter_Init_Success") == 0) {
            RUN_TEST(test_BpFilter_Init_Success);
        } else if (strcmp(test_name, "test_BpFilter_Init_Failure") == 0) {
            RUN_TEST(test_BpFilter_Init_Failure);
        } else if (strcmp(test_name, "test_Bp_add_sink_Success") == 0) {
            RUN_TEST(test_Bp_add_sink_Success);
        } else if (strcmp(test_name, "test_Bp_add_multiple_sinks") == 0) {
            RUN_TEST(test_Bp_add_multiple_sinks);
        } else if (strcmp(test_name, "test_Bp_remove_sink_Success") == 0) {
            RUN_TEST(test_Bp_remove_sink_Success);
        } else if (strcmp(test_name, "test_multi_transform_function") == 0) {
            RUN_TEST(test_multi_transform_function);
        } else if (strcmp(test_name, "test_Bp_Filter_Start_Success") == 0) {
            RUN_TEST(test_Bp_Filter_Start_Success);
        } else if (strcmp(test_name, "test_Bp_Filter_Start_Already_Running") == 0) {
            RUN_TEST(test_Bp_Filter_Start_Already_Running);
        } else if (strcmp(test_name, "test_Bp_Filter_Stop_Success") == 0) {
            RUN_TEST(test_Bp_Filter_Stop_Success);
        } else if (strcmp(test_name, "test_Bp_Filter_Stop_Not_Running") == 0) {
            RUN_TEST(test_Bp_Filter_Stop_Not_Running);
        } else if (strcmp(test_name, "test_Bp_Filter_Start_Null_Filter") == 0) {
            RUN_TEST(test_Bp_Filter_Start_Null_Filter);
        } else if (strcmp(test_name, "test_Bp_Filter_Stop_Null_Filter") == 0) {
            RUN_TEST(test_Bp_Filter_Stop_Null_Filter);
        } else if (strcmp(test_name, "test_Overflow_Behavior_Block_Default") == 0) {
            RUN_TEST(test_Overflow_Behavior_Block_Default);
        } else if (strcmp(test_name, "test_Overflow_Behavior_Drop_Mode") == 0) {
            RUN_TEST(test_Overflow_Behavior_Drop_Mode);
        } else if (strcmp(test_name, "test_Await_Timeout_Behavior") == 0) {
            RUN_TEST(test_Await_Timeout_Behavior);
        } else if (strcmp(test_name, "test_Await_Stopped_Behavior") == 0) {
            RUN_TEST(test_Await_Stopped_Behavior);
        }
    } else {
        // Run all tests with debug output
        printf("Running test_BpFilter_Init_Success\n");
        RUN_TEST(test_BpFilter_Init_Success);
        printf("Running test_BpFilter_Init_Failure\n");
        RUN_TEST(test_BpFilter_Init_Failure);
        printf("Running test_Bp_add_sink_Success\n");
        RUN_TEST(test_Bp_add_sink_Success);
        printf("Running test_Bp_add_multiple_sinks\n");
        RUN_TEST(test_Bp_add_multiple_sinks);
        printf("Running test_Bp_remove_sink_Success\n");
        RUN_TEST(test_Bp_remove_sink_Success);
        printf("Running test_multi_transform_function\n");
        RUN_TEST(test_multi_transform_function);
        printf("Running test_Bp_Filter_Start_Success\n");
        RUN_TEST(test_Bp_Filter_Start_Success);
        printf("Running test_Bp_Filter_Start_Already_Running\n");
        RUN_TEST(test_Bp_Filter_Start_Already_Running);
        printf("Running test_Bp_Filter_Stop_Success\n");
        RUN_TEST(test_Bp_Filter_Stop_Success);
        printf("Running test_Bp_Filter_Stop_Not_Running\n");
        RUN_TEST(test_Bp_Filter_Stop_Not_Running);
        printf("Running test_Bp_Filter_Start_Null_Filter\n");
        RUN_TEST(test_Bp_Filter_Start_Null_Filter);
        printf("Running test_Bp_Filter_Stop_Null_Filter\n");
        RUN_TEST(test_Bp_Filter_Stop_Null_Filter);
        printf("Running test_Overflow_Behavior_Block_Default\n");
        RUN_TEST(test_Overflow_Behavior_Block_Default);
        printf("Running test_Overflow_Behavior_Drop_Mode\n");
        RUN_TEST(test_Overflow_Behavior_Drop_Mode);
        printf("Running test_Await_Timeout_Behavior\n");
        RUN_TEST(test_Await_Timeout_Behavior);
        printf("Running test_Await_Stopped_Behavior\n");
        RUN_TEST(test_Await_Stopped_Behavior);
    }
    
    return UNITY_END();
}
