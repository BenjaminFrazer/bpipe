/*
 * Simple CSV Scale Example
 * 
 * This example demonstrates reading and scaling CSV data
 * without using the CSV sink (which has issues)
 */

#include "../bpipe/csv_source.h"
#include "../bpipe/map.h"
#include "../bpipe/core.h"
#include "../bpipe/utils.h"
#include "../bpipe/bperr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// Global scale factor
static float g_scale_factor = 1.0f;

// Custom map function for scaling
static Bp_EC map_scale_f32(const void* in, void* out, size_t n_samples)
{
    if (!in || !out) return Bp_EC_NULL_POINTER;
    
    const float* input = (const float*)in;
    float* output = (float*)out;
    
    for (size_t i = 0; i < n_samples; i++) {
        output[i] = input[i] * g_scale_factor;
    }
    
    return Bp_EC_OK;
}

// Simple sink that prints data
typedef struct {
    Filter_t base;
    FILE* output_file;
} PrintSink_t;

static void* print_sink_worker(void* arg)
{
    PrintSink_t* sink = (PrintSink_t*)arg;
    Bp_EC err;
    
    while (atomic_load(&sink->base.running)) {
        Batch_t* batch = bb_get_tail(&sink->base.input_buffers[0], 
                                     sink->base.timeout_us, &err);
        if (!batch) {
            if (err == Bp_EC_TIMEOUT) continue;
            if (err == Bp_EC_STOPPED) break;
            break;
        }
        
        // Check for completion
        if (batch->ec == Bp_EC_COMPLETE) {
            bb_del_tail(&sink->base.input_buffers[0]);
            break;
        }
        
        // Print the data
        float* data = (float*)batch->data;
        for (size_t i = batch->tail; i < batch->head; i++) {
            uint64_t timestamp = batch->t_ns + (i - batch->tail) * batch->period_ns;
            fprintf(sink->output_file, "%lu,%.3f\n", timestamp, data[i]);
        }
        
        bb_del_tail(&sink->base.input_buffers[0]);
    }
    
    return NULL;
}

int main(int argc, char* argv[])
{
    if (argc != 4) {
        printf("Usage: %s <input.csv> <output.csv> <scale_factor>\n", argv[0]);
        return 1;
    }
    
    const char* input_file = argv[1];
    const char* output_file = argv[2];
    g_scale_factor = atof(argv[3]);
    
    // 1. Create CSV source
    CsvSource_t csv_source = {0};
    CsvSource_config_t csv_config = {
        .name = "csv_reader",
        .file_path = input_file,
        .delimiter = ',',
        .has_header = true,
        .ts_column_name = "timestamp_ns",
        .data_column_names = {"sensor1", NULL},  // Single column
        .detect_regular_timing = true,
        .regular_threshold_ns = 10000,
        .output_dtype = DTYPE_FLOAT,
        .batch_size = 64,
        .ring_capacity = 256,
        .loop = false,
        .skip_invalid = true,
        .timeout_us = 1000000
    };
    
    Bp_EC err = csvsource_init(&csv_source, csv_config);
    if (err != Bp_EC_OK) {
        printf("Failed to initialize CSV source: %s\n", err_lut[err]);
        return 1;
    }
    
    // 2. Create map filter
    Map_filt_t scaler;
    Map_config_t map_config = {
        .name = "scaler",
        .buff_config = {
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 6,
            .ring_capacity_expo = 8,
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        .map_fcn = map_scale_f32,
        .timeout_us = 1000000
    };
    
    err = map_init(&scaler, map_config);
    if (err != Bp_EC_OK) {
        printf("Failed to initialize map filter: %s\n", err_lut[err]);
        csvsource_destroy(&csv_source);
        return 1;
    }
    
    // 3. Create simple print sink
    PrintSink_t sink = {0};
    sink.output_file = fopen(output_file, "w");
    if (!sink.output_file) {
        printf("Failed to open output file: %s\n", output_file);
        filt_deinit(&scaler.base);
        csvsource_destroy(&csv_source);
        return 1;
    }
    
    // Write header
    fprintf(sink.output_file, "timestamp_ns,scaled_value\n");
    
    // Initialize sink filter
    Core_filt_config_t sink_config = {
        .name = "print_sink",
        .filt_type = FILT_T_MAP,
        .size = sizeof(PrintSink_t),
        .n_inputs = 1,
        .max_supported_sinks = 0,
        .buff_config = {
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 6,
            .ring_capacity_expo = 8,
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        .timeout_us = 1000000,
        .worker = print_sink_worker
    };
    
    err = filt_init(&sink.base, sink_config);
    if (err != Bp_EC_OK) {
        printf("Failed to initialize sink: %s\n", err_lut[err]);
        fclose(sink.output_file);
        filt_deinit(&scaler.base);
        csvsource_destroy(&csv_source);
        return 1;
    }
    
    // 4. Connect pipeline
    err = filt_sink_connect(&csv_source.base, 0, &scaler.base.input_buffers[0]);
    if (err != Bp_EC_OK) {
        printf("Failed to connect source to scaler\n");
        goto cleanup;
    }
    
    err = filt_sink_connect(&scaler.base, 0, &sink.base.input_buffers[0]);
    if (err != Bp_EC_OK) {
        printf("Failed to connect scaler to sink\n");
        goto cleanup;
    }
    
    // 5. Start pipeline
    filt_start(&sink.base);
    filt_start(&scaler.base);
    filt_start(&csv_source.base);
    
    printf("Processing %s with scale factor %.2f -> %s\n", 
           input_file, g_scale_factor, output_file);
    
    // 6. Wait for completion
    pthread_join(csv_source.base.worker_thread, NULL);
    pthread_join(scaler.base.worker_thread, NULL);
    pthread_join(sink.base.worker_thread, NULL);
    
    printf("Processing complete!\n");
    
cleanup:
    filt_stop(&csv_source.base);
    filt_stop(&scaler.base);
    filt_stop(&sink.base);
    
    filt_deinit(&sink.base);
    filt_deinit(&scaler.base);
    csvsource_destroy(&csv_source);
    
    fclose(sink.output_file);
    
    return 0;
}