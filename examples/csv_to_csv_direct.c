/*
 * CSV to CSV Direct Copy Example
 * Tests if CSV source -> CSV sink works without map filter
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bpipe/csv_source.h"
#include "bpipe/csv_sink.h"
#include "bpipe/core.h"
#include "bpipe/utils.h"
#include "bpipe/bperr.h"

int main(int argc, char* argv[])
{
    if (argc != 4) {
        printf("Usage: %s <input.csv> <output.csv> <column_name>\n", argv[0]);
        return 1;
    }
    
    const char* input_file = argv[1];
    const char* output_file = argv[2];
    const char* target_column = argv[3];
    
    printf("CSV to CSV Direct Copy\n");
    printf("======================\n");
    printf("Input: %s\n", input_file);
    printf("Output: %s\n", output_file);
    printf("Column: %s\n\n", target_column);
    
    // Initialize CSV source
    CsvSource_t csv_source = {0};
    CsvSource_config_t csv_config = {
        .name = "csv_reader",
        .file_path = input_file,
        .delimiter = ',',
        .has_header = true,
        .ts_column_name = "timestamp_ns",
        .detect_regular_timing = true,
        .regular_threshold_ns = 10000,
        .loop = false,
        .skip_invalid = true,
        .timeout_us = 1000000
    };
    
    csv_config.data_column_names[0] = target_column;
    csv_config.data_column_names[1] = NULL;
    
    Bp_EC err = csvsource_init(&csv_source, csv_config);
    if (err != Bp_EC_OK) {
        printf("Failed to init CSV source: %d\n", err);
        return 1;
    }
    
    // Initialize CSV sink
    CSVSink_t csv_sink = {0};
    CSVSink_config_t sink_config = {
        .name = "csv_writer",
        .buff_config = {
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 6,
            .ring_capacity_expo = 8,
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        .output_path = output_file,
        .append = false,
        .file_mode = 0644,
        .max_file_size_bytes = 0,
        .format = CSV_FORMAT_SIMPLE,
        .delimiter = ",",
        .line_ending = "\n",
        .write_header = true,
        .column_names = NULL,
        .n_columns = 1,
        .precision = 3
    };
    
    err = csv_sink_init(&csv_sink, sink_config);
    if (err != Bp_EC_OK) {
        printf("Failed to init CSV sink: %d\n", err);
        csvsource_destroy(&csv_source);
        return 1;
    }
    
    // Connect directly
    err = filt_sink_connect(&csv_source.base, 0, csv_sink.base.input_buffers[0]);
    if (err != Bp_EC_OK) {
        printf("Failed to connect: %d\n", err);
        goto cleanup;
    }
    
    // Start filters
    err = filt_start(&csv_source.base);
    if (err != Bp_EC_OK) {
        printf("Failed to start source: %d\n", err);
        goto cleanup;
    }
    
    err = filt_start(&csv_sink.base);
    if (err != Bp_EC_OK) {
        printf("Failed to start sink: %d\n", err);
        filt_stop(&csv_source.base);
        goto cleanup;
    }
    
    // Wait
    usleep(1000000);
    
    // Stop
    filt_stop(&csv_source.base);
    filt_stop(&csv_sink.base);
    
    printf("Source read: %lu samples\n", csv_source.base.metrics.samples_processed);
    printf("Sink wrote: %lu samples\n", csv_sink.samples_written);
    
cleanup:
    filt_deinit(&csv_sink.base);
    csvsource_destroy(&csv_source);
    
    return (err == Bp_EC_OK) ? 0 : 1;
}