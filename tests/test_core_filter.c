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
    // Clean up code for the test filter
    BpFilter_Deinit(&test_filter);
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

    // Clean up - stop the filter
    Bp_Filter_Stop(&filter);
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
    Bp_Filter_Start(&filter);

    // Try to start again - should fail with specific error code
    Bp_EC result = Bp_Filter_Start(&filter);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_ALREADY_RUNNING, result);

    // Clean up
    Bp_Filter_Stop(&filter);
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
    Bp_Filter_Start(&filter);
    TEST_ASSERT_TRUE(filter.running);

    Bp_EC result = Bp_Filter_Stop(&filter);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, result);
    TEST_ASSERT_TRUE(!filter.running);
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
    BpFilter_Init(&filter, &config);

    // Default behavior should be OVERFLOW_BLOCK
    TEST_ASSERT_EQUAL_UINT(OVERFLOW_BLOCK, filter.overflow_behaviour);
}

void test_Overflow_Behavior_Drop_Mode(void)
{
    Bp_Filter_t filter;
    BpFilterConfig config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_UNSIGNED,
        .buffer_size = 128,
        .batch_size = 64,
        .number_of_batches_exponent = 2,
        .number_of_input_filters = 1
    };
    BpFilter_Init(&filter, &config);
    filter.overflow_behaviour = OVERFLOW_DROP;
    filter.running = true;  // Set running to true for testing
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
    filter.input_buffers[0].head = 0;  // Reset to non-full state
    filter.input_buffers[0].tail = 0;

    Bp_Batch_t batch3 = Bp_allocate(&filter, &filter.input_buffers[0]);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_OK, batch3.ec);
    TEST_ASSERT_NOT_NULL(batch3.data);

    // Clean up
    filter.running = false;  // Reset running flag
    Bp_deallocate_buffers(&filter, 0);
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
    BpFilter_Init(&filter, &config);
    filter.timeout.tv_sec = 0;
    filter.timeout.tv_nsec = 100000000;  // 100ms timeout
    
    Bp_BatchBuffer_t* buf = &filter.input_buffers[0];
    
    // Test timeout on empty buffer
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // Should timeout after 100ms since buffer is empty
    Bp_EC result = Bp_await_not_empty(buf, 100000);  // 100ms in microseconds
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    TEST_ASSERT_EQUAL(Bp_EC_TIMEOUT, result);
    
    // Verify it actually waited approximately 100ms
    long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
    TEST_ASSERT_TRUE(elapsed_ms >= 95 && elapsed_ms <= 150);  // Allow some tolerance
    
    // Clean up
    Bp_deallocate_buffers(&filter, 0);
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
    BpFilter_Init(&filter, &config);
    
    Bp_BatchBuffer_t* buf = &filter.input_buffers[0];
    
    // Stop the buffer
    BpBatchBuffer_stop(buf);
    
    // Both await functions should return STOPPED immediately
    Bp_EC result1 = Bp_await_not_empty(buf, 0);  // No timeout
    TEST_ASSERT_EQUAL(Bp_EC_STOPPED, result1);
    
    Bp_EC result2 = Bp_await_not_full(buf, 0);   // No timeout
    TEST_ASSERT_EQUAL(Bp_EC_STOPPED, result2);
    
    // Clean up
    Bp_deallocate_buffers(&filter, 0);
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
