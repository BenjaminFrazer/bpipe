/*
 * Simplified CSV to CSV Scale Example
 * 
 * This example demonstrates scaling a single column from a CSV file.
 */

#define _GNU_SOURCE  // For usleep
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../bpipe/csv_source.h"
#include "../bpipe/csv_sink.h"
#include "../bpipe/map.h"
#include "../bpipe/core.h"
#include "../bpipe/utils.h"
#include "../bpipe/bperr.h"

// Global scale factor (accessed by map function)
static float g_scale_factor = 1.0f;

// Custom map function for scaling float values
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

int main(int argc, char* argv[])
{
    Bp_EC err;
    
    // Check command line arguments
    if (argc != 5) {
        printf("Usage: %s <input.csv> <output.csv> <scale_factor> <column_name>\n", argv[0]);
        printf("Example: %s data/sensor_data.csv data/scaled.csv 2.5 sensor1\n", argv[0]);
        return 1;
    }
    
    const char* input_file = argv[1];
    const char* output_file = argv[2];
    g_scale_factor = atof(argv[3]);
    const char* target_column = argv[4];
    
    printf("CSV to CSV Scale Example (Simplified)\n");
    printf("=====================================\n");
    printf("Input file: %s\n", input_file);
    printf("Output file: %s\n", output_file);
    printf("Scale factor: %.2f\n", g_scale_factor);
    printf("Target column: %s\n", target_column);
    printf("\n");
    
    // 1. Create CSV source filter
    CsvSource_t csv_source = {0};
    CsvSource_config_t csv_config = {
        .name = "csv_reader",
        .file_path = input_file,
        .delimiter = ',',
        .has_header = true,
        .ts_column_name = "timestamp_ns",
        .data_column_names = {target_column, NULL},  // Process only the specified column
        .detect_regular_timing = true,
        .regular_threshold_ns = 10000,  // 10μs tolerance
        .loop = false,
        .skip_invalid = true,
        .timeout_us = 1000000  // 1 second
    };
    
    printf("Initializing CSV source...\n");
    err = csvsource_init(&csv_source, csv_config);
    if (err != Bp_EC_OK) {
        printf("Failed to initialize CSV source: %d (%s)\n", err, 
               err < Bp_EC_MAX ? err_lut[err] : "Unknown error");
        return 1;
    }
    
    // Verify we got exactly one column
    if (csv_source.n_data_columns != 1) {
        printf("Error: Expected 1 data column, but got %zu\n", csv_source.n_data_columns);
        csvsource_destroy(&csv_source);
        return 1;
    }
    
    // 2. Create a map filter to scale values
    printf("Initializing map filter...\n");
    Map_filt_t scaler;
    Map_config_t map_config = {
        .name = "scaler",
        .buff_config = {
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 6,  // 64 samples
            .ring_capacity_expo = 8,   // 256 batches
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        .map_fcn = map_scale_f32,
        .timeout_us = 1000000
    };
    
    err = map_init(&scaler, map_config);
    if (err != Bp_EC_OK) {
        printf("Failed to initialize map filter: %d\n", err);
        csvsource_destroy(&csv_source);
        return 1;
    }
    
    // 3. Create CSV sink filter
    printf("Initializing CSV sink...\n");
    CSVSink_t csv_sink;
    
    CSVSink_config_t sink_config = {
        .name = "csv_writer",
        .buff_config = {
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 6,  // 64 samples
            .ring_capacity_expo = 8,   // 256 batches
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        .output_path = output_file,
        .append = false,
        .file_mode = 0644,
        .max_file_size_bytes = 0,  // Unlimited
        .format = CSV_FORMAT_SIMPLE,
        .delimiter = ",",
        .line_ending = "\n",
        .write_header = true,
        .column_names = NULL,  // Use default "value"
        .n_columns = 1,  // Single data column
        .precision = 3
    };
    
    err = csv_sink_init(&csv_sink, sink_config);
    if (err != Bp_EC_OK) {
        printf("Failed to initialize CSV sink: %d\n", err);
        filt_deinit(&scaler.base);
        csvsource_destroy(&csv_source);
        return 1;
    }
    
    // 4. Connect the pipeline: CSV source -> Map filter -> CSV sink
    printf("Connecting pipeline...\n");
    
    // CSV source output 0 (first/only data column) -> Map input
    err = filt_sink_connect(&csv_source.base, 0, &scaler.base.input_buffers[0]);
    if (err != Bp_EC_OK) {
        printf("Failed to connect CSV source to map: %d\n", err);
        goto cleanup;
    }
    
    // Map output -> CSV sink input
    err = filt_sink_connect(&scaler.base, 0, &csv_sink.base.input_buffers[0]);
    if (err != Bp_EC_OK) {
        printf("Failed to connect map to CSV sink: %d\n", err);
        goto cleanup;
    }
    
    // 5. Start the pipeline (in reverse order)
    printf("Starting pipeline...\n");
    
    err = filt_start(&csv_sink.base);
    if (err != Bp_EC_OK) {
        printf("Failed to start CSV sink: %d\n", err);
        goto cleanup;
    }
    
    err = filt_start(&scaler.base);
    if (err != Bp_EC_OK) {
        printf("Failed to start map filter: %d\n", err);
        filt_stop(&csv_sink.base);
        goto cleanup;
    }
    
    err = filt_start(&csv_source.base);
    if (err != Bp_EC_OK) {
        printf("Failed to start CSV source: %d\n", err);
        filt_stop(&scaler.base);
        filt_stop(&csv_sink.base);
        goto cleanup;
    }
    
    printf("\nProcessing data...\n");
    
    // 6. Wait for a moment to let pipeline process
    usleep(1000000);  // 1 second
    
    // 7. Stop all filters (this will join threads)
    printf("Stopping pipeline...\n");
    filt_stop(&csv_source.base);
    filt_stop(&scaler.base);
    filt_stop(&csv_sink.base);
    
    // Check for errors
    if (csv_source.base.worker_err_info.ec != Bp_EC_OK) {
        printf("CSV source error: %d\n", csv_source.base.worker_err_info.ec);
        if (csv_source.base.worker_err_info.err_msg) {
            printf("  Message: %s\n", csv_source.base.worker_err_info.err_msg);
        }
    }
    if (scaler.base.worker_err_info.ec != Bp_EC_OK) {
        printf("Map filter error: %d\n", scaler.base.worker_err_info.ec);
        if (scaler.base.worker_err_info.err_msg) {
            printf("  Message: %s\n", scaler.base.worker_err_info.err_msg);
        }
    }
    if (csv_sink.base.worker_err_info.ec != Bp_EC_OK) {
        printf("CSV sink error: %d\n", csv_sink.base.worker_err_info.ec);
        if (csv_sink.base.worker_err_info.err_msg) {
            printf("  Message: %s\n", csv_sink.base.worker_err_info.err_msg);
        }
    }
    
    // Print statistics
    printf("\nProcessing complete!\n");
    printf("Samples processed: %lu\n", csv_sink.samples_written);
    printf("Output written to: %s\n", output_file);
    
cleanup:
    // Destroy filters and free memory
    filt_deinit(&csv_sink.base);
    filt_deinit(&scaler.base);
    csvsource_destroy(&csv_source);
    
    return (err == Bp_EC_OK) ? 0 : 1;
}