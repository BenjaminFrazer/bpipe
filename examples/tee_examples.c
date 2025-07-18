#include "../bpipe/tee.h"
#include "../bpipe/map.h"
#include <stdio.h>
#include <unistd.h>

// Example 1: Basic data distribution to processing and logging
void example_processing_and_logging(void) {
    printf("Example 1: Processing and Logging Pipeline\n");
    printf("==========================================\n");
    
    // Configure tee with 2 outputs
    // Output 0: Critical processing path (blocking)
    // Output 1: Logging/monitoring path (dropping)
    BatchBuffer_config out_configs[2] = {
        {
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 8,      // 256 samples per batch
            .ring_capacity_expo = 5,       // 31 batches
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        {
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 8,      // 256 samples per batch
            .ring_capacity_expo = 3,       // 7 batches (smaller for logging)
            .overflow_behaviour = OVERFLOW_DROP_TAIL  // Best-effort
        }
    };
    
    Tee_config_t tee_config = {
        .name = "processing_tee",
        .buff_config = out_configs[0],  // Input buffer config
        .n_outputs = 2,
        .output_configs = out_configs,
        .timeout_us = 1000,
        .copy_data = true
    };
    
    // Initialize tee filter
    Tee_filt_t tee;
    if (tee_init(&tee, tee_config) != Bp_EC_OK) {
        printf("Failed to initialize tee\n");
        return;
    }
    
    // Create processing pipeline (e.g., signal processing)
    Map_filt_t processor;
    Map_config_t proc_config = {
        .name = "signal_processor",
        .buff_config = out_configs[0],
        .map_fcn = map_identity_f32,  // Would be actual processing
        .timeout_us = 1000
    };
    
    if (map_init(&processor, proc_config) != Bp_EC_OK) {
        printf("Failed to initialize processor\n");
        filt_deinit(&tee.base);
        return;
    }
    
    // Create logging sink
    Batch_buff_t log_buffer;
    if (bb_init(&log_buffer, "log_sink", out_configs[1]) != Bp_EC_OK) {
        printf("Failed to initialize log buffer\n");
        filt_deinit(&tee.base);
        filt_deinit(&processor.base);
        return;
    }
    
    // Connect pipeline: source → tee → [processor, logger]
    filt_sink_connect(&tee.base, 0, &processor.base.input_buffers[0]);
    filt_sink_connect(&tee.base, 1, &log_buffer);
    
    // Create final output for processed data
    Batch_buff_t processed_output;
    bb_init(&processed_output, "processed", out_configs[0]);
    filt_sink_connect(&processor.base, 0, &processed_output);
    
    // Start all components
    filt_start(&tee.base);
    filt_start(&processor.base);
    bb_start(&log_buffer);
    bb_start(&processed_output);
    
    printf("Pipeline configured:\n");
    printf("  Input → Tee → [Critical Processing, Best-Effort Logging]\n");
    printf("  - Processing path: BLOCKING (no data loss)\n");
    printf("  - Logging path: DROPPING (won't slow down processing)\n\n");
    
    // Simulate data input
    printf("Submitting test data...\n");
    for (int i = 0; i < 5; i++) {
        Batch_t* batch = bb_get_head(&tee.base.input_buffers[0]);
        if (batch) {
            float* data = (float*)batch->data;
            for (int j = 0; j < 256; j++) {
                data[j] = (float)(i * 256 + j);
            }
            batch->head = 256;
            batch->t_ns = i * 1000000;  // 1ms intervals
            bb_submit(&tee.base.input_buffers[0], 1000);
        }
    }
    
    // Let processing happen
    usleep(50000);  // 50ms
    
    // Check results
    printf("Tee statistics:\n");
    printf("  - Successful writes to processing: %zu\n", tee.successful_writes[0]);
    printf("  - Successful writes to logging: %zu\n", tee.successful_writes[1]);
    
    // Cleanup
    filt_stop(&tee.base);
    filt_stop(&processor.base);
    bb_stop(&log_buffer);
    bb_stop(&processed_output);
    filt_deinit(&tee.base);
    filt_deinit(&processor.base);
    bb_deinit(&log_buffer);
    bb_deinit(&processed_output);
    
    printf("\nExample completed!\n\n");
}

// Example 2: Redundant processing paths
void example_redundant_processing(void) {
    printf("Example 2: Redundant Processing for Fault Tolerance\n");
    printf("==================================================\n");
    
    // Configure tee with 3 outputs for triple redundancy
    BatchBuffer_config out_configs[3];
    for (int i = 0; i < 3; i++) {
        out_configs[i] = (BatchBuffer_config){
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 7,      // 128 samples
            .ring_capacity_expo = 4,       // 15 batches
            .overflow_behaviour = OVERFLOW_BLOCK
        };
    }
    
    Tee_config_t tee_config = {
        .name = "redundancy_tee",
        .buff_config = out_configs[0],  // Input buffer config
        .n_outputs = 3,
        .output_configs = out_configs,
        .timeout_us = 1000,
        .copy_data = true
    };
    
    // Initialize tee
    Tee_filt_t tee;
    if (tee_init(&tee, tee_config) != Bp_EC_OK) {
        printf("Failed to initialize tee\n");
        return;
    }
    
    // Create 3 identical processing paths
    Map_filt_t processors[3];
    Batch_buff_t outputs[3];
    
    for (int i = 0; i < 3; i++) {
        char name[32];
        snprintf(name, sizeof(name), "processor_%d", i);
        
        Map_config_t config = {
            .name = name,
            .buff_config = out_configs[i],
            .map_fcn = map_identity_f32,  // Would be actual processing
            .timeout_us = 1000
        };
        
        if (map_init(&processors[i], config) != Bp_EC_OK) {
            printf("Failed to initialize processor %d\n", i);
            // Cleanup and return...
            return;
        }
        
        snprintf(name, sizeof(name), "output_%d", i);
        bb_init(&outputs[i], name, out_configs[i]);
        
        // Connect: tee → processor → output
        filt_sink_connect(&tee.base, i, &processors[i].base.input_buffers[0]);
        filt_sink_connect(&processors[i].base, 0, &outputs[i]);
        
        // Start components
        filt_start(&processors[i].base);
        bb_start(&outputs[i]);
    }
    
    filt_start(&tee.base);
    
    printf("Triple redundancy pipeline configured:\n");
    printf("  Input → Tee → [Processor0, Processor1, Processor2]\n");
    printf("  All paths are identical for fault tolerance\n\n");
    
    // Simulate data
    printf("Processing data through redundant paths...\n");
    for (int i = 0; i < 3; i++) {
        Batch_t* batch = bb_get_head(&tee.base.input_buffers[0]);
        if (batch) {
            float* data = (float*)batch->data;
            for (int j = 0; j < 128; j++) {
                data[j] = (float)(i * 128 + j);
            }
            batch->head = 128;
            bb_submit(&tee.base.input_buffers[0], 1000);
        }
    }
    
    usleep(50000);
    
    // Verify all paths processed data
    printf("Verification:\n");
    for (int i = 0; i < 3; i++) {
        printf("  - Path %d processed: %zu batches\n", i, tee.successful_writes[i]);
    }
    
    // Cleanup
    filt_stop(&tee.base);
    for (int i = 0; i < 3; i++) {
        filt_stop(&processors[i].base);
        bb_stop(&outputs[i]);
        filt_deinit(&processors[i].base);
        bb_deinit(&outputs[i]);
    }
    filt_deinit(&tee.base);
    
    printf("\nExample completed!\n\n");
}

// Example 3: Multi-rate monitoring
void example_multirate_monitoring(void) {
    printf("Example 3: Multi-rate Monitoring\n");
    printf("================================\n");
    
    // Configure tee with different batch sizes for different monitoring rates
    BatchBuffer_config out_configs[3] = {
        {
            // High-rate processing
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 6,      // 64 samples (high rate)
            .ring_capacity_expo = 6,       // 63 batches
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        {
            // Medium-rate monitoring
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 8,      // 256 samples (medium rate)
            .ring_capacity_expo = 4,       // 15 batches
            .overflow_behaviour = OVERFLOW_DROP_TAIL
        },
        {
            // Low-rate statistics
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 10,     // 1024 samples (low rate)
            .ring_capacity_expo = 3,       // 7 batches
            .overflow_behaviour = OVERFLOW_DROP_TAIL
        }
    };
    
    Tee_config_t tee_config = {
        .name = "multirate_tee",
        .buff_config = out_configs[0],  // Input buffer config  
        .n_outputs = 3,
        .output_configs = out_configs,
        .timeout_us = 1000,
        .copy_data = true
    };
    
    Tee_filt_t tee;
    if (tee_init(&tee, tee_config) != Bp_EC_OK) {
        printf("Failed to initialize tee\n");
        return;
    }
    
    // Create monitoring sinks
    Batch_buff_t high_rate, med_rate, low_rate;
    bb_init(&high_rate, "high_rate", out_configs[0]);
    bb_init(&med_rate, "med_rate", out_configs[1]);
    bb_init(&low_rate, "low_rate", out_configs[2]);
    
    // Connect outputs
    filt_sink_connect(&tee.base, 0, &high_rate);
    filt_sink_connect(&tee.base, 1, &med_rate);
    filt_sink_connect(&tee.base, 2, &low_rate);
    
    // Start components
    filt_start(&tee.base);
    bb_start(&high_rate);
    bb_start(&med_rate);
    bb_start(&low_rate);
    
    printf("Multi-rate monitoring configured:\n");
    printf("  - Output 0: 64 samples/batch (high-rate, blocking)\n");
    printf("  - Output 1: 256 samples/batch (medium-rate, dropping)\n");
    printf("  - Output 2: 1024 samples/batch (low-rate, dropping)\n\n");
    
    printf("Note: This demonstrates the concept, but map filter's\n");
    printf("      batch size handling would affect actual behavior.\n\n");
    
    // Cleanup
    filt_stop(&tee.base);
    bb_stop(&high_rate);
    bb_stop(&med_rate);
    bb_stop(&low_rate);
    filt_deinit(&tee.base);
    bb_deinit(&high_rate);
    bb_deinit(&med_rate);
    bb_deinit(&low_rate);
    
    printf("Example completed!\n\n");
}

int main(void) {
    printf("Tee Filter Examples\n");
    printf("===================\n\n");
    
    example_processing_and_logging();
    example_redundant_processing();
    example_multirate_monitoring();
    
    printf("All examples completed!\n");
    return 0;
}