#include "../bpipe/csv_source.h"
#include "../bpipe/core.h"
#include "../bpipe/bperr.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

int main(int argc, char* argv[])
{
    if (argc != 2) {
        printf("Usage: %s <input.csv>\n", argv[0]);
        return 1;
    }
    
    const char* input_file = argv[1];
    
    // Create CSV source filter
    CsvSource_t csv_source = {0};
    CsvSource_config_t csv_config = {
        .name = "csv_reader",
        .file_path = input_file,
        .delimiter = ',',
        .has_header = true,
        .ts_column_name = "timestamp_ns",
        .data_column_names = {"sensor1", "sensor2", "sensor3", NULL},
        .detect_regular_timing = true,
        .regular_threshold_ns = 10000,
        .output_dtype = DTYPE_FLOAT,
        .batch_size = 64,
        .ring_capacity = 256,
        .loop = false,
        .skip_invalid = true,
        .timeout_us = 1000000
    };
    
    printf("Initializing CSV source...\n");
    Bp_EC err = csvsource_init(&csv_source, csv_config);
    if (err != Bp_EC_OK) {
        printf("Failed to initialize CSV source: %d (%s)\n", err, 
               err < Bp_EC_MAX ? err_lut[err] : "Unknown error");
        return 1;
    }
    
    printf("CSV source initialized successfully\n");
    printf("Number of data columns: %zu\n", csv_source.n_data_columns);
    
    // Clean up
    csvsource_destroy(&csv_source);
    
    return 0;
}