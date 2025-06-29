#include "bpipe/core.h"
#include <stdio.h>
#include <assert.h>

void test_filter_with_enhanced_buffers() {
    printf("Testing filter initialization with enhanced buffers...\n");
    
    // Create filter with enhanced buffer configuration
    BpFilterConfig config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_FLOAT,
        .buffer_size = 128,
        .batch_size = 32,
        .number_of_batches_exponent = 4,
        .number_of_input_filters = 1,
        .overflow_behaviour = OVERFLOW_BLOCK,
        .auto_allocate_buffers = true,
        .timeout_us = 1000000
    };
    
    Bp_Filter_t filter;
    Bp_EC result = BpFilter_Init(&filter, &config);
    assert(result == Bp_EC_OK);
    
    // Check that the input buffer was initialized with enhanced properties
    Bp_BatchBuffer_t* buffer = &filter.input_buffers[0];
    assert(buffer->dtype == DTYPE_FLOAT);
    assert(buffer->data_width == sizeof(float));
    assert(buffer->overflow_behaviour == OVERFLOW_BLOCK);
    assert(buffer->timeout_us == 1000000);
    assert(Bp_batch_capacity(buffer) == 32);
    
    // Test buffer-centric operations on filter's input buffer
    assert(BpBatchBuffer_IsEmpty(buffer));
    assert(BpBatchBuffer_Capacity(buffer) == 16);  // 2^4
    
    // Test that we can allocate and submit using buffer-centric API
    Bp_Batch_t batch = BpBatchBuffer_Allocate(buffer);
    assert(batch.ec == Bp_EC_OK);
    assert(batch.dtype == DTYPE_FLOAT);
    
    // Fill with test data
    float* data = (float*)batch.data;
    for (int i = 0; i < 16; i++) {
        data[i] = (float)i;
    }
    batch.head = 16;
    
    result = BpBatchBuffer_Submit(buffer, &batch);
    assert(result == Bp_EC_OK);
    assert(!BpBatchBuffer_IsEmpty(buffer));
    
    // Test legacy compatibility - old API should still work
    Bp_Batch_t legacy_batch = Bp_head(&filter, buffer);
    assert(legacy_batch.ec == Bp_EC_OK);
    assert(legacy_batch.head == 16);
    
    // Verify data through legacy API
    float* legacy_data = (float*)legacy_batch.data;
    for (int i = 0; i < 16; i++) {
        assert(legacy_data[i] == (float)i);
    }
    
    Bp_delete_tail(&filter, buffer);
    assert(BpBatchBuffer_IsEmpty(buffer));
    
    BpFilter_Deinit(&filter);
    printf("✓ Filter with enhanced buffers test passed\n");
}

void test_mixed_api_usage() {
    printf("Testing mixed API usage (legacy + buffer-centric)...\n");
    
    BpFilterConfig config = BP_CONFIG_FLOAT_STANDARD;
    config.batch_size = 8;
    config.number_of_batches_exponent = 3;  // 8 batches
    
    Bp_Filter_t source, sink;
    Bp_EC result = BpFilter_Init(&source, &config);
    assert(result == Bp_EC_OK);
    
    result = BpFilter_Init(&sink, &config);
    assert(result == Bp_EC_OK);
    
    // Connect using legacy API
    result = Bp_add_sink(&source, &sink);
    assert(result == Bp_EC_OK);
    
    // Use buffer-centric API for operations
    Bp_BatchBuffer_t* sink_buffer = &sink.input_buffers[0];
    
    // Source allocates using legacy API
    Bp_Batch_t batch = Bp_allocate(&source, sink_buffer);
    assert(batch.ec == Bp_EC_OK);
    
    // Fill with data using buffer-centric thinking but legacy batch
    float* data = (float*)batch.data;
    for (int i = 0; i < 4; i++) {
        data[i] = (float)i * 2.0f;
    }
    batch.head = 4;
    
    // Submit using legacy API
    Bp_submit_batch(&source, sink_buffer, &batch);
    
    // Read using buffer-centric API
    Bp_Batch_t received = BpBatchBuffer_Head(sink_buffer);
    assert(received.ec == Bp_EC_OK);
    assert(received.head == 4);
    
    // Verify data
    float* received_data = (float*)received.data;
    for (int i = 0; i < 4; i++) {
        assert(received_data[i] == (float)i * 2.0f);
    }
    
    // Delete using buffer-centric API
    result = BpBatchBuffer_DeleteTail(sink_buffer);
    assert(result == Bp_EC_OK);
    assert(BpBatchBuffer_IsEmpty(sink_buffer));
    
    BpFilter_Deinit(&source);
    BpFilter_Deinit(&sink);
    printf("✓ Mixed API usage test passed\n");
}

void test_buffer_statistics_integration() {
    printf("Testing buffer statistics in filter context...\n");
    
    BpFilterConfig config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_INT,
        .buffer_size = 64,
        .batch_size = 4,
        .number_of_batches_exponent = 2,  // 4 batches
        .number_of_input_filters = 1,
        .overflow_behaviour = OVERFLOW_DROP,
        .auto_allocate_buffers = true,
        .timeout_us = 10000  // 10ms timeout
    };
    
    Bp_Filter_t filter;
    Bp_EC result = BpFilter_Init(&filter, &config);
    assert(result == Bp_EC_OK);
    
    Bp_BatchBuffer_t* buffer = &filter.input_buffers[0];
    
    // Fill buffer completely
    for (int i = 0; i < 4; i++) {
        Bp_Batch_t batch = BpBatchBuffer_Allocate(buffer);
        assert(batch.ec == Bp_EC_OK);
        batch.head = 4;
        BpBatchBuffer_Submit(buffer, &batch);
    }
    
    assert(buffer->total_batches == 4);
    assert(BpBatchBuffer_IsFull(buffer));
    
    // Try overflow - should drop
    Bp_Batch_t overflow = BpBatchBuffer_Allocate(buffer);
    assert(overflow.ec == Bp_EC_NOSPACE);
    assert(buffer->dropped_batches == 1);
    
    // Test runtime configuration changes
    result = BpBatchBuffer_SetOverflowBehaviour(buffer, OVERFLOW_BLOCK);
    assert(result == Bp_EC_OK);
    assert(buffer->overflow_behaviour == OVERFLOW_BLOCK);
    
    BpFilter_Deinit(&filter);
    printf("✓ Buffer statistics integration test passed\n");
}

int main() {
    printf("Running Filter Integration Tests\n");
    printf("================================\n");
    
    test_filter_with_enhanced_buffers();
    test_mixed_api_usage();
    test_buffer_statistics_integration();
    
    printf("\n✓ All filter integration tests passed!\n");
    return 0;
}