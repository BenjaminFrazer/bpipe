/*
 * CSV to CSV Scale Example
 * 
 * This example demonstrates a complete pipeline that:
 * 1. Reads time-series data from a CSV file
 * 2. Scales all values using a map filter
 * 3. Writes the scaled data to a destination CSV file
 */

#include "../bpipe/csv_source.h"
#include "../bpipe/csv_sink.h"
#include "../bpipe/map.h"
#include "../bpipe/core.h"
#include "../bpipe/utils.h"
#include "../bpipe/bperr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// Example input CSV format:
// timestamp_ns,sensor1,sensor2,sensor3
// 1000000000,100.0,200.0,300.0
// 1001000000,101.0,201.0,301.0
// 1002000000,102.0,202.0,302.0

// External error lookup table is already declared in bperr.h

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
    if (argc != 4) {
        printf("Usage: %s <input.csv> <output.csv> <scale_factor>\n", argv[0]);
        printf("Example: %s data/input.csv data/output.csv 2.5\n", argv[0]);
        return 1;
    }
    
    const char* input_file = argv[1];
    const char* output_file = argv[2];
    g_scale_factor = atof(argv[3]);
    
    // 1. Create CSV source filter
    CsvSource_t csv_source = {0};
    CsvSource_config_t csv_config = {
        .name = "csv_reader",
        .file_path = input_file,
        .delimiter = ',',
        .has_header = true,
        .ts_column_name = "timestamp_ns",
        // Explicitly specify data columns due to bug in auto-detection
        .data_column_names = {"sensor1", NULL},  // Single column only
        .detect_regular_timing = true,
        .regular_threshold_ns = 10000,  // 10μs tolerance
        .output_dtype = DTYPE_FLOAT,
        .batch_size = 64,
        .ring_capacity = 256,
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
    printf("CSV source initialized successfully\n");
    
    // Get the number of data channels from the CSV source
    size_t n_channels = csv_source.n_data_columns;
    printf("Detected %zu data channels in CSV file\n", n_channels);
    
    // 2. Create a map filter to scale all values
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
    printf("Map filter initialized successfully\n");
    
    // 3. Create CSV sink filter
    CSVSink_t csv_sink;
    
    // Create column names for output (timestamp + scaled data columns)
    printf("Creating output columns for %zu channels\n", n_channels);
    const char** output_columns = malloc((n_channels + 1) * sizeof(char*));
    output_columns[0] = "timestamp_ns";
    for (size_t i = 0; i < n_channels; i++) {
        char* col_name = malloc(32);
        snprintf(col_name, 32, "scaled_ch%zu", i);
        output_columns[i + 1] = col_name;
    }
    
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
        .format = n_channels == 1 ? CSV_FORMAT_SIMPLE : CSV_FORMAT_MULTI_COL,
        .delimiter = ",",
        .line_ending = "\n",
        .write_header = true,
        .column_names = output_columns,
        .n_columns = n_channels,  // Data columns only (timestamp is handled separately)
        .precision = 3
    };
    
    err = csv_sink_init(&csv_sink, sink_config);
    if (err != Bp_EC_OK) {
        printf("Failed to initialize CSV sink: %d\n", err);
        filt_deinit(&scaler.base);
        csvsource_destroy(&csv_source);
        for (size_t i = 0; i < n_channels; i++) {
            free((char*)output_columns[i + 1]);
        }
        free(output_columns);
        return 1;
    }
    
    // 4. Connect the pipeline: CSV source -> Map filter -> CSV sink
    err = filt_sink_connect(&csv_source.base, 0, &scaler.base.input_buffers[0]);
    if (err != Bp_EC_OK) {
        printf("Failed to connect CSV source to map: %d\n", err);
        goto cleanup;
    }
    
    err = filt_sink_connect(&scaler.base, 0, &csv_sink.base.input_buffers[0]);
    if (err != Bp_EC_OK) {
        printf("Failed to connect map to CSV sink: %d\n", err);
        goto cleanup;
    }
    
    // 5. Start the pipeline (in reverse order)
    filt_start(&csv_sink.base);
    filt_start(&scaler.base);
    filt_start(&csv_source.base);
    
    printf("Processing CSV data with scale factor %.2f...\n", g_scale_factor);
    printf("Input: %s\n", input_file);
    printf("Output: %s\n", output_file);
    
    // 6. Wait for completion using pthread_join
    if (csv_source.base.worker) {
        pthread_join(csv_source.base.worker_thread, NULL);
    }
    if (scaler.base.worker) {
        pthread_join(scaler.base.worker_thread, NULL);
    }
    if (csv_sink.base.worker) {
        pthread_join(csv_sink.base.worker_thread, NULL);
    }
    
    // Check for errors
    if (csv_source.base.worker_err_info.ec != Bp_EC_OK) {
        printf("CSV source error: %d\n", csv_source.base.worker_err_info.ec);
    }
    if (scaler.base.worker_err_info.ec != Bp_EC_OK) {
        printf("Map filter error: %d\n", scaler.base.worker_err_info.ec);
    }
    if (csv_sink.base.worker_err_info.ec != Bp_EC_OK) {
        printf("CSV sink error: %d\n", csv_sink.base.worker_err_info.ec);
    }
    
    // Print statistics
    printf("\nProcessing complete!\n");
    printf("Samples processed: %lu\n", csv_sink.samples_written);
    printf("Batches processed: %lu\n", csv_sink.batches_processed);
    printf("Lines written: %lu\n", csv_sink.lines_written);
    
cleanup:
    // Stop all filters
    filt_stop(&csv_source.base);
    filt_stop(&scaler.base);
    filt_stop(&csv_sink.base);
    
    // Destroy filters and free memory
    filt_deinit(&csv_sink.base);
    filt_deinit(&scaler.base);
    csvsource_destroy(&csv_source);
    
    // Free column names
    for (size_t i = 0; i < n_channels; i++) {
        free((char*)output_columns[i + 1]);
    }
    free(output_columns);
    
    return (err == Bp_EC_OK) ? 0 : 1;
}