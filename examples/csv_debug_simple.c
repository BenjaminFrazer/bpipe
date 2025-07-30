/*
 * Simple CSV to Debug test
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "bpipe/csv_source.h"
#include "bpipe/debug_output_filter.h"
#include "bpipe/core.h"
#include "bpipe/bperr.h"

int main(int argc, char* argv[])
{
    if (argc != 3) {
        printf("Usage: %s <input.csv> <column>\n", argv[0]);
        return 1;
    }
    
    // Initialize CSV source
    CsvSource_t csv_source = {0};
    CsvSource_config_t csv_config = {
        .name = "csv_reader",
        .file_path = argv[1],
        .delimiter = ',',
        .has_header = true,
        .ts_column_name = "timestamp_ns",
        .detect_regular_timing = true,
        .regular_threshold_ns = 10000,
        .loop = false,
        .skip_invalid = true,
        .timeout_us = 1000000
    };
    
    csv_config.data_column_names[0] = argv[2];
    csv_config.data_column_names[1] = NULL;
    
    Bp_EC err = csvsource_init(&csv_source, csv_config);
    if (err != Bp_EC_OK) {
        printf("CSV init failed: %d\n", err);
        return 1;
    }
    
    // Initialize debug filter
    DebugOutputFilter_t debug_filter = {0};
    DebugOutputConfig_t debug_config = {
        .prefix = "DEBUG: ",
        .filename = NULL,  // stdout
        .show_metadata = true,
        .show_samples = true,
        .max_samples_per_batch = -1,  // all
        .format = DEBUG_FMT_DECIMAL,
        .flush_after_print = true
    };
    
    err = debug_output_filter_init(&debug_filter, &debug_config);
    if (err != Bp_EC_OK) {
        printf("Debug init failed: %d\n", err);
        csvsource_destroy(&csv_source);
        return 1;
    }
    
    // Connect
    err = filt_sink_connect(&csv_source.base, 0, debug_filter.base.input_buffers[0]);
    if (err != Bp_EC_OK) {
        printf("Connect failed: %d\n", err);
        goto cleanup;
    }
    
    // Start
    err = filt_start(&csv_source.base);
    if (err != Bp_EC_OK) goto cleanup;
    
    err = filt_start(&debug_filter.base);
    if (err != Bp_EC_OK) {
        filt_stop(&csv_source.base);
        goto cleanup;
    }
    
    // Wait
    usleep(500000);
    
    // Stop
    filt_stop(&csv_source.base);
    filt_stop(&debug_filter.base);
    
    printf("\nSource: %lu samples\n", csv_source.base.metrics.samples_processed);
    
cleanup:
    filt_deinit(&debug_filter.base);
    csvsource_destroy(&csv_source);
    return 0;
}