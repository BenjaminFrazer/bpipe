/*
 * CSV to CSV Scale Example
 * 
 * This example demonstrates scaling a single column from a CSV file.
 * Due to architectural constraints, processing multiple columns requires
 * separate pipelines and output files for each column.
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
#include "../bpipe/debug_output_filter.h"

// Example input CSV format:
// timestamp_ns,sensor1,sensor2,sensor3
// 1000000000,100.0,200.0,300.0
// 1001000000,101.0,201.0,301.0
// 1002000000,102.0,202.0,302.0

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
    
    printf("CSV to CSV Scale Example\n");
    printf("========================\n");
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
    printf("  - file_path: %s\n", csv_config.file_path);
    printf("  - ts_column_name: %s\n", csv_config.ts_column_name);
    printf("  - data_column_names[0]: %s\n", csv_config.data_column_names[0]);
    
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
    
    // 3. Create debug filters to help diagnose the issue
    printf("Initializing debug filters...\n");
    DebugOutputFilter_t debug1, debug2;
    
    DebugOutputConfig_t debug1_config = {
        .prefix = "DEBUG1 (after CSV source): ",
        .filename = "debug1_output.txt",
        .append_mode = false,
        .show_metadata = true,
        .show_samples = true,
        .max_samples_per_batch = 5,
        .format = DEBUG_FMT_DECIMAL,
        .flush_after_print = true
    };
    
    DebugOutputConfig_t debug2_config = {
        .prefix = "DEBUG2 (after map): ",
        .filename = "debug2_output.txt",
        .append_mode = false,
        .show_metadata = true,
        .show_samples = true,
        .max_samples_per_batch = 5,
        .format = DEBUG_FMT_DECIMAL,
        .flush_after_print = true
    };
    
    err = debug_output_filter_init(&debug1, &debug1_config);
    if (err != Bp_EC_OK) {
        printf("Failed to initialize debug1 filter: %d\n", err);
        filt_deinit(&scaler.base);
        csvsource_destroy(&csv_source);
        return 1;
    }
    
    err = debug_output_filter_init(&debug2, &debug2_config);
    if (err != Bp_EC_OK) {
        printf("Failed to initialize debug2 filter: %d\n", err);
        filt_deinit(&debug1.base);
        filt_deinit(&scaler.base);
        csvsource_destroy(&csv_source);
        return 1;
    }
    
    // 4. Create CSV sink filter
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
        filt_deinit(&debug2.base);
        filt_deinit(&debug1.base);
        filt_deinit(&scaler.base);
        csvsource_destroy(&csv_source);
        return 1;
    }
    
    // 5. Connect the pipeline: CSV source -> Debug1 -> Map filter -> Debug2 -> CSV sink
    printf("Connecting pipeline...\n");
    
    // CSV source output 0 (first/only data column) -> Debug1 input
    err = filt_sink_connect(&csv_source.base, 0, debug1.base.input_buffers[0]);
    if (err != Bp_EC_OK) {
        printf("Failed to connect CSV source to debug1: %d\n", err);
        goto cleanup;
    }
    
    // Debug1 output -> Map input
    err = filt_sink_connect(&debug1.base, 0, scaler.base.input_buffers[0]);
    if (err != Bp_EC_OK) {
        printf("Failed to connect debug1 to map: %d\n", err);
        goto cleanup;
    }
    
    // Map output -> Debug2 input
    err = filt_sink_connect(&scaler.base, 0, debug2.base.input_buffers[0]);
    if (err != Bp_EC_OK) {
        printf("Failed to connect map to debug2: %d\n", err);
        goto cleanup;
    }
    
    // Debug2 output -> CSV sink input
    err = filt_sink_connect(&debug2.base, 0, csv_sink.base.input_buffers[0]);
    if (err != Bp_EC_OK) {
        printf("Failed to connect debug2 to CSV sink: %d\n", err);
        goto cleanup;
    }
    
    // 6. Start the pipeline (in reverse order)
    printf("Starting pipeline...\n");
    
    err = filt_start(&csv_sink.base);
    if (err != Bp_EC_OK) {
        printf("Failed to start CSV sink: %d\n", err);
        goto cleanup;
    }
    
    err = filt_start(&debug2.base);
    if (err != Bp_EC_OK) {
        printf("Failed to start debug2 filter: %d\n", err);
        filt_stop(&csv_sink.base);
        goto cleanup;
    }
    
    err = filt_start(&scaler.base);
    if (err != Bp_EC_OK) {
        printf("Failed to start map filter: %d\n", err);
        filt_stop(&debug2.base);
        filt_stop(&csv_sink.base);
        goto cleanup;
    }
    
    err = filt_start(&debug1.base);
    if (err != Bp_EC_OK) {
        printf("Failed to start debug1 filter: %d\n", err);
        filt_stop(&scaler.base);
        filt_stop(&debug2.base);
        filt_stop(&csv_sink.base);
        goto cleanup;
    }
    
    err = filt_start(&csv_source.base);
    if (err != Bp_EC_OK) {
        printf("Failed to start CSV source: %d\n", err);
        filt_stop(&debug1.base);
        filt_stop(&scaler.base);
        filt_stop(&debug2.base);
        filt_stop(&csv_sink.base);
        goto cleanup;
    }
    
    printf("\nProcessing data...\n");
    
    // 6. Wait for a moment to let pipeline process
    usleep(1000000);  // 1 second
    
    // 8. Stop all filters (this will join threads)
    printf("Stopping pipeline...\n");
    filt_stop(&csv_source.base);
    filt_stop(&debug1.base);
    filt_stop(&scaler.base);
    filt_stop(&debug2.base);
    filt_stop(&csv_sink.base);
    
    // Check for errors
    if (csv_source.base.worker_err_info.ec != Bp_EC_OK) {
        printf("CSV source error: %d\n", csv_source.base.worker_err_info.ec);
        if (csv_source.base.worker_err_info.err_msg) {
            printf("  Message: %s\n", csv_source.base.worker_err_info.err_msg);
        }
    }
    if (debug1.base.worker_err_info.ec != Bp_EC_OK) {
        printf("Debug1 filter error: %d\n", debug1.base.worker_err_info.ec);
        if (debug1.base.worker_err_info.err_msg) {
            printf("  Message: %s\n", debug1.base.worker_err_info.err_msg);
        }
    }
    if (scaler.base.worker_err_info.ec != Bp_EC_OK) {
        printf("Map filter error: %d\n", scaler.base.worker_err_info.ec);
        if (scaler.base.worker_err_info.err_msg) {
            printf("  Message: %s\n", scaler.base.worker_err_info.err_msg);
        }
    }
    if (debug2.base.worker_err_info.ec != Bp_EC_OK) {
        printf("Debug2 filter error: %d\n", debug2.base.worker_err_info.ec);
        if (debug2.base.worker_err_info.err_msg) {
            printf("  Message: %s\n", debug2.base.worker_err_info.err_msg);
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
    filt_deinit(&debug2.base);
    filt_deinit(&scaler.base);
    filt_deinit(&debug1.base);
    csvsource_destroy(&csv_source);
    
    return (err == Bp_EC_OK) ? 0 : 1;
}