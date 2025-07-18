#include "tee.h"
#include <stdio.h>
#include <unistd.h>

/**
 * Example 1: Basic dual output tee
 * 
 * Demonstrates splitting one input stream to two identical outputs.
 * This is useful for:
 * - Logging data while processing
 * - Sending data to multiple processing paths
 * - Creating data backups
 */
void example_basic_dual_output(void)
{
    printf("\n=== Example: Basic Dual Output ===\n");
    
    // Configure two outputs with same data type
    BatchBuffer_config output_configs[2] = {
        {
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 6,  // 64 samples per batch
            .ring_capacity_expo = 4,   // 15 batches in ring
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        {
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 6,  // 64 samples per batch
            .ring_capacity_expo = 4,   // 15 batches in ring
            .overflow_behaviour = OVERFLOW_BLOCK
        }
    };
    
    // Configure tee filter
    Tee_config_t config = {
        .name = "dual_output_tee",
        .buff_config = output_configs[0],  // Input buffer uses same config as outputs
        .n_outputs = 2,
        .output_configs = output_configs,
        .timeout_us = 1000000,  // 1 second timeout
        .copy_data = true       // Deep copy for data integrity
    };
    
    // Initialize tee
    Tee_filt_t tee;
    Bp_EC err = tee_init(&tee, config);
    if (err != Bp_EC_OK) {
        printf("Failed to initialize tee: %d\n", err);
        return;
    }
    
    // Create output buffers
    Batch_buff_t output1, output2;
    bb_init(&output1, "output1", output_configs[0]);
    bb_init(&output2, "output2", output_configs[1]);
    
    // Connect outputs to tee
    filt_sink_connect(&tee.base, 0, &output1);
    filt_sink_connect(&tee.base, 1, &output2);
    
    // Start processing
    filt_start(&tee.base);
    bb_start(&tee.base.input_buffers[0]);
    bb_start(&output1);
    bb_start(&output2);
    
    // Submit some test data
    for (int i = 0; i < 3; i++) {
        Batch_t* batch = bb_get_head(&tee.base.input_buffers[0]);
        if (batch) {
            float* data = (float*)batch->data;
            for (int j = 0; j < 64; j++) {
                data[j] = i * 64.0f + j;
            }
            batch->head = 64;
            batch->t_ns = i * 1000000000LL;  // 1 second intervals
            bb_submit(&tee.base.input_buffers[0], 1000000);
        }
    }
    
    // Wait for processing
    usleep(100000);  // 100ms
    
    // Read from both outputs
    printf("Output 1 received %zu batches\n", tee.successful_writes[0]);
    printf("Output 2 received %zu batches\n", tee.successful_writes[1]);
    
    // Cleanup
    bb_stop(&tee.base.input_buffers[0]);
    filt_stop(&tee.base);
    bb_stop(&output1);
    bb_stop(&output2);
    filt_deinit(&tee.base);
    bb_deinit(&output1);
    bb_deinit(&output2);
}

/**
 * Example 2: Multi-output with different overflow behaviors
 * 
 * Demonstrates using different overflow behaviors for different outputs.
 * This is useful when:
 * - One output is critical (must not lose data)
 * - Other outputs are for monitoring (can drop data if needed)
 */
void example_mixed_overflow_behavior(void)
{
    printf("\n=== Example: Mixed Overflow Behavior ===\n");
    
    // Configure three outputs with different behaviors
    BatchBuffer_config output_configs[3] = {
        {
            .dtype = DTYPE_I32,
            .batch_capacity_expo = 5,  // 32 samples per batch
            .ring_capacity_expo = 3,   // 7 batches (small buffer)
            .overflow_behaviour = OVERFLOW_BLOCK  // Critical path - never drop
        },
        {
            .dtype = DTYPE_I32,
            .batch_capacity_expo = 5,  // 32 samples per batch
            .ring_capacity_expo = 3,   // 7 batches (small buffer)
            .overflow_behaviour = OVERFLOW_DROP_TAIL  // Monitor - can drop old data
        },
        {
            .dtype = DTYPE_I32,
            .batch_capacity_expo = 5,  // 32 samples per batch
            .ring_capacity_expo = 3,   // 7 batches (small buffer)
            .overflow_behaviour = OVERFLOW_DROP_HEAD  // Logger - drop new if full
        }
    };
    
    Tee_config_t config = {
        .name = "mixed_behavior_tee",
        .buff_config = output_configs[0],  // Input buffer config
        .n_outputs = 3,
        .output_configs = output_configs,
        .timeout_us = 100000,  // 100ms timeout
        .copy_data = true
    };
    
    // Initialize and connect (similar to example 1)
    Tee_filt_t tee;
    tee_init(&tee, config);
    
    // ... rest of setup ...
    
    printf("Configured 3 outputs:\n");
    printf("  Output 0: OVERFLOW_BLOCK (critical)\n");
    printf("  Output 1: OVERFLOW_DROP_TAIL (monitor)\n");
    printf("  Output 2: OVERFLOW_DROP_HEAD (logger)\n");
}

/**
 * Example 3: Variable batch sizes
 * 
 * Demonstrates handling different batch sizes on outputs.
 * Current implementation truncates data when output buffer is smaller.
 * 
 * Use cases:
 * - Downsampling data for different consumers
 * - Adapting to different processing requirements
 */
void example_variable_batch_sizes(void)
{
    printf("\n=== Example: Variable Batch Sizes ===\n");
    
    // Input: 256 samples per batch
    BatchBuffer_config input_config = {
        .dtype = DTYPE_FLOAT,
        .batch_capacity_expo = 8,  // 256 samples
        .ring_capacity_expo = 4,   // 15 batches
        .overflow_behaviour = OVERFLOW_BLOCK
    };
    
    // Outputs with different batch sizes
    BatchBuffer_config output_configs[3] = {
        {
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 6,  // 64 samples (smaller)
            .ring_capacity_expo = 5,   // 31 batches
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        {
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 8,  // 256 samples (same as input)
            .ring_capacity_expo = 4,   // 15 batches
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        {
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 9,  // 512 samples (larger)
            .ring_capacity_expo = 3,   // 7 batches
            .overflow_behaviour = OVERFLOW_BLOCK
        }
    };
    
    Tee_config_t config = {
        .name = "variable_size_tee",
        .buff_config = input_config,
        .n_outputs = 3,
        .output_configs = output_configs,
        .timeout_us = 1000000,
        .copy_data = true
    };
    
    // Initialize tee
    Tee_filt_t tee;
    tee_init(&tee, config);
    
    printf("Input batch size: 256 samples\n");
    printf("Output 0: 64 samples (will truncate to first 64)\n");
    printf("Output 1: 256 samples (full data)\n");
    printf("Output 2: 512 samples (will receive 256 samples per batch)\n");
    
    // Note: Full variable batch size support with accumulation
    // would require maintaining partial batch state
}

/**
 * Example 4: Pipeline integration
 * 
 * Shows how to use tee in a processing pipeline to create
 * parallel processing paths.
 */
void example_pipeline_integration(void)
{
    printf("\n=== Example: Pipeline Integration ===\n");
    
    // This example would show:
    // 1. Input source -> Tee
    // 2. Tee output 0 -> Processing path A -> Result A
    // 3. Tee output 1 -> Processing path B -> Result B
    
    printf("Pipeline structure:\n");
    printf("                  ┌─> Process A ─> Result A\n");
    printf("  Input ─> Tee ─┤\n");
    printf("                  └─> Process B ─> Result B\n");
}

// Main function to run all examples
int main(void)
{
    printf("Tee Filter Examples\n");
    printf("==================\n");
    
    example_basic_dual_output();
    example_mixed_overflow_behavior();
    example_variable_batch_sizes();
    example_pipeline_integration();
    
    return 0;
}