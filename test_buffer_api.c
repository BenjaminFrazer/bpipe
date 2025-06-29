#include "bpipe/core.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

void test_buffer_config_init() {
    printf("Testing BpBatchBuffer_InitFromConfig...\n");
    
    BpBufferConfig_t config = {
        .batch_size = 64,
        .number_of_batches = 16,
        .data_width = sizeof(float),
        .dtype = DTYPE_FLOAT,
        .overflow_behaviour = OVERFLOW_BLOCK,
        .timeout_us = 1000000,
        .name = "test_buffer"
    };
    
    Bp_BatchBuffer_t buffer;
    Bp_EC result = BpBatchBuffer_InitFromConfig(&buffer, &config);
    assert(result == Bp_EC_OK);
    
    // Check configuration was copied correctly
    assert(buffer.data_width == sizeof(float));
    assert(buffer.dtype == DTYPE_FLOAT);
    assert(buffer.overflow_behaviour == OVERFLOW_BLOCK);
    assert(buffer.timeout_us == 1000000);
    assert(strcmp(buffer.name, "test_buffer") == 0);
    
    // Check capacity calculations
    assert(BpBatchBuffer_Capacity(&buffer) == 16);
    assert(Bp_batch_capacity(&buffer) == 64);
    
    // Check initial state
    assert(BpBatchBuffer_IsEmpty(&buffer));
    assert(!BpBatchBuffer_IsFull(&buffer));
    assert(BpBatchBuffer_Available(&buffer) == 0);
    
    Bp_BatchBuffer_Deinit(&buffer);
    printf("✓ BpBatchBuffer_InitFromConfig test passed\n");
}

void test_buffer_create_destroy() {
    printf("Testing BpBatchBuffer_Create/Destroy...\n");
    
    BpBufferConfig_t config = BP_BUFFER_CONFIG_DEFAULT;
    config.dtype = DTYPE_INT;
    config.data_width = sizeof(int);
    config.name = "created_buffer";
    
    Bp_BatchBuffer_t* buffer = BpBatchBuffer_Create(&config);
    assert(buffer != NULL);
    
    assert(buffer->dtype == DTYPE_INT);
    assert(buffer->data_width == sizeof(int));
    assert(strcmp(buffer->name, "created_buffer") == 0);
    
    BpBatchBuffer_Destroy(buffer);
    printf("✓ BpBatchBuffer_Create/Destroy test passed\n");
}

void test_buffer_operations() {
    printf("Testing buffer operations...\n");
    
    BpBufferConfig_t config = {
        .batch_size = 4,
        .number_of_batches = 4,
        .data_width = sizeof(float),
        .dtype = DTYPE_FLOAT,
        .overflow_behaviour = OVERFLOW_BLOCK,
        .timeout_us = 100000,  // 100ms timeout
        .name = "ops_test"
    };
    
    Bp_BatchBuffer_t buffer;
    Bp_EC result = BpBatchBuffer_InitFromConfig(&buffer, &config);
    assert(result == Bp_EC_OK);
    
    // Test allocate and submit
    Bp_Batch_t batch = BpBatchBuffer_Allocate(&buffer);
    assert(batch.ec == Bp_EC_OK);
    assert(batch.capacity == 4);
    assert(batch.dtype == DTYPE_FLOAT);
    
    // Fill batch with test data
    float* data = (float*)batch.data;
    for (int i = 0; i < 4; i++) {
        data[i] = (float)i * 1.5f;
    }
    batch.head = 4;  // Mark as full
    
    result = BpBatchBuffer_Submit(&buffer, &batch);
    assert(result == Bp_EC_OK);
    assert(!BpBatchBuffer_IsEmpty(&buffer));
    assert(BpBatchBuffer_Available(&buffer) == 1);
    
    // Test head and delete
    Bp_Batch_t head_batch = BpBatchBuffer_Head(&buffer);
    assert(head_batch.ec == Bp_EC_OK);
    assert(head_batch.head == 4);
    
    // Verify data
    float* head_data = (float*)head_batch.data;
    for (int i = 0; i < 4; i++) {
        assert(head_data[i] == (float)i * 1.5f);
    }
    
    result = BpBatchBuffer_DeleteTail(&buffer);
    assert(result == Bp_EC_OK);
    assert(BpBatchBuffer_IsEmpty(&buffer));
    
    Bp_BatchBuffer_Deinit(&buffer);
    printf("✓ Buffer operations test passed\n");
}

void test_buffer_statistics() {
    printf("Testing buffer statistics...\n");
    
    BpBufferConfig_t config = {
        .batch_size = 2,
        .number_of_batches = 2,
        .data_width = sizeof(int),
        .dtype = DTYPE_INT,
        .overflow_behaviour = OVERFLOW_DROP,
        .timeout_us = 1000,  // Very short timeout
        .name = "stats_test"
    };
    
    Bp_BatchBuffer_t buffer;
    Bp_EC result = BpBatchBuffer_InitFromConfig(&buffer, &config);
    assert(result == Bp_EC_OK);
    
    // Initial statistics
    assert(buffer.total_batches == 0);
    assert(buffer.dropped_batches == 0);
    
    // Fill buffer completely
    for (int i = 0; i < 2; i++) {
        Bp_Batch_t batch = BpBatchBuffer_Allocate(&buffer);
        assert(batch.ec == Bp_EC_OK);
        batch.head = 2;  // Mark as full
        BpBatchBuffer_Submit(&buffer, &batch);
    }
    
    assert(buffer.total_batches == 2);
    assert(BpBatchBuffer_IsFull(&buffer));
    
    // Try to allocate when full with drop behavior - should fail
    Bp_Batch_t overflow_batch = BpBatchBuffer_Allocate(&buffer);
    assert(overflow_batch.ec == Bp_EC_NOSPACE);
    assert(buffer.dropped_batches == 1);
    
    Bp_BatchBuffer_Deinit(&buffer);
    printf("✓ Buffer statistics test passed\n");
}

void test_buffer_configuration_updates() {
    printf("Testing buffer configuration updates...\n");
    
    BpBufferConfig_t config = BP_BUFFER_CONFIG_DEFAULT;
    Bp_BatchBuffer_t buffer;
    Bp_EC result = BpBatchBuffer_InitFromConfig(&buffer, &config);
    assert(result == Bp_EC_OK);
    
    // Test timeout update
    assert(buffer.timeout_us == 1000000);
    result = BpBatchBuffer_SetTimeout(&buffer, 500000);
    assert(result == Bp_EC_OK);
    assert(buffer.timeout_us == 500000);
    
    // Test overflow behavior update  
    assert(buffer.overflow_behaviour == OVERFLOW_BLOCK);
    result = BpBatchBuffer_SetOverflowBehaviour(&buffer, OVERFLOW_DROP);
    assert(result == Bp_EC_OK);
    assert(buffer.overflow_behaviour == OVERFLOW_DROP);
    
    Bp_BatchBuffer_Deinit(&buffer);
    printf("✓ Buffer configuration updates test passed\n");
}

int main() {
    printf("Running Buffer-Centric API Tests\n");
    printf("=================================\n");
    
    test_buffer_config_init();
    test_buffer_create_destroy();
    test_buffer_operations();
    test_buffer_statistics();
    test_buffer_configuration_updates();
    
    printf("\n✓ All buffer API tests passed!\n");
    return 0;
}