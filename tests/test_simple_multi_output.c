#define _GNU_SOURCE  // For usleep
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include "../bpipe/signal_gen.h"
#include "unity.h"

void setUp(void)
{
    // Setup code for each test
}

void tearDown(void)
{
    // Cleanup code for each test
}

/* Test that multiple sinks can be added and removed */
void test_multiple_sink_connections(void)
{
    Bp_Filter_t source;
    Bp_Filter_t sink1, sink2, sink3;
    
    // Initialize filters
    BpFilter_Init(&source, BpPassThroughTransform, 0, 128, 64, 6, 0);
    BpFilter_Init(&sink1, BpPassThroughTransform, 0, 128, 64, 6, 1);
    BpFilter_Init(&sink2, BpPassThroughTransform, 0, 128, 64, 6, 1);
    BpFilter_Init(&sink3, BpPassThroughTransform, 0, 128, 64, 6, 1);
    
    // Test adding sinks
    TEST_ASSERT_EQUAL(0, source.n_sinks);
    
    TEST_ASSERT_EQUAL(Bp_EC_OK, Bp_add_sink(&source, &sink1));
    TEST_ASSERT_EQUAL(1, source.n_sinks);
    TEST_ASSERT_EQUAL(&sink1, source.sinks[0]);
    
    TEST_ASSERT_EQUAL(Bp_EC_OK, Bp_add_sink(&source, &sink2));
    TEST_ASSERT_EQUAL(2, source.n_sinks);
    TEST_ASSERT_EQUAL(&sink2, source.sinks[1]);
    
    TEST_ASSERT_EQUAL(Bp_EC_OK, Bp_add_sink(&source, &sink3));
    TEST_ASSERT_EQUAL(3, source.n_sinks);
    TEST_ASSERT_EQUAL(&sink3, source.sinks[2]);
    
    // Test removing sinks
    TEST_ASSERT_EQUAL(Bp_EC_OK, Bp_remove_sink(&source, &sink2));
    TEST_ASSERT_EQUAL(2, source.n_sinks);
    TEST_ASSERT_EQUAL(&sink1, source.sinks[0]);
    TEST_ASSERT_EQUAL(&sink3, source.sinks[1]);
    TEST_ASSERT_NULL(source.sinks[2]);
}

/* Test that data is automatically distributed to multiple outputs */
void test_multi_output_data_distribution(void)
{
    // Create a signal generator with 3 outputs
    Bp_SignalGen_t source;
    Bp_Filter_t sink1, sink2, sink3;
    
    // Initialize with square wave
    BpSignalGen_Init(&source, BP_WAVE_SQUARE, 10.0f, 1.0f, 0.0f, 0.0f, 128, 64, 6);
    BpFilter_Init(&sink1, BpPassThroughTransform, 0, 128, 64, 6, 1);
    BpFilter_Init(&sink2, BpPassThroughTransform, 0, 128, 64, 6, 1);
    BpFilter_Init(&sink3, BpPassThroughTransform, 0, 128, 64, 6, 1);
    
    // Set data properties for sinks to match source
    sink1.dtype = DTYPE_FLOAT;
    sink1.data_width = sizeof(float);
    sink2.dtype = DTYPE_FLOAT;
    sink2.data_width = sizeof(float);
    sink3.dtype = DTYPE_FLOAT;
    sink3.data_width = sizeof(float);
    
    // Allocate buffers for sinks
    Bp_allocate_buffers(&sink1, 0);
    Bp_allocate_buffers(&sink2, 0);
    Bp_allocate_buffers(&sink3, 0);
    
    // Connect all sinks to source
    Bp_add_sink(&source.base, &sink1);
    Bp_add_sink(&source.base, &sink2);
    Bp_add_sink(&source.base, &sink3);
    
    // Start all filters
    Bp_Filter_Start(&source.base);
    Bp_Filter_Start(&sink1);
    Bp_Filter_Start(&sink2);
    Bp_Filter_Start(&sink3);
    
    // Let some data flow
    usleep(10000);  // 10ms
    
    // Stop source first to ensure completion propagates
    Bp_Filter_Stop(&source.base);
    
    // Stop sinks
    Bp_Filter_Stop(&sink1);
    Bp_Filter_Stop(&sink2);
    Bp_Filter_Stop(&sink3);
    
    // Verify all sinks received data
    TEST_ASSERT_TRUE(sink1.input_buffers[0].head > 0);
    TEST_ASSERT_TRUE(sink2.input_buffers[0].head > 0);
    TEST_ASSERT_TRUE(sink3.input_buffers[0].head > 0);
    
    // Verify all sinks received the same amount of data
    size_t data_received1 = sink1.input_buffers[0].head;
    size_t data_received2 = sink2.input_buffers[0].head;
    size_t data_received3 = sink3.input_buffers[0].head;
    
    TEST_ASSERT_EQUAL(data_received1, data_received2);
    TEST_ASSERT_EQUAL(data_received1, data_received3);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_multiple_sink_connections);
    RUN_TEST(test_multi_output_data_distribution);
    return UNITY_END();
}