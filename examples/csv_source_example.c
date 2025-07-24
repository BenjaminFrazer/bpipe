/*
 * CSV Source Example
 * 
 * This example demonstrates how to use the CSV source filter to read
 * time-series data from a CSV file and process it through a pipeline.
 */

#include "csv_source.h"
#include "map.h"
#include "batch_buffer.h"
#include <stdio.h>
#include <unistd.h>

// Example CSV content:
// timestamp_ns,temperature,humidity,pressure
// 1000000000,22.5,65.2,1013.25
// 1001000000,22.6,65.0,1013.24
// 1002000000,22.7,64.8,1013.23

int main(void)
{
    Bp_EC err;
    
    // 1. Create CSV source filter
    CsvSource_t csv_source;
    CsvSource_config_t csv_config = {
        .name = "weather_data",
        .file_path = "data/weather.csv",
        .delimiter = ',',
        .has_header = true,
        .ts_column_name = "timestamp_ns",
        .data_column_names = {"temperature", "humidity", "pressure", NULL},
        .detect_regular_timing = true,
        .regular_threshold_ns = 10000,  // 10μs tolerance
        .output_dtype = DTYPE_FLOAT,
        .batch_size = 64,
        .ring_capacity = 256,
        .loop = false,
        .skip_invalid = true,
        .timeout_us = 1000000  // 1 second
    };
    
    err = csvsource_init(&csv_source, csv_config);
    if (err != Bp_EC_OK) {
        printf("Failed to initialize CSV source: %d\n", err);
        return 1;
    }
    
    // 2. Create a processing filter (scale temperature to Fahrenheit)
    Map_t temp_converter;
    Map_config_t map_config = {
        .name = "temp_to_fahrenheit",
        .n_channels = 3,  // Process all 3 channels
        .func = MAP_FUNC_SCALE,
        .scale = 1.8,     // C to F conversion
        .offset = 32.0,   // C to F offset
        .buff_config = {
            .dtype = DTYPE_FLOAT,
            .batch_capacity_expo = 6,  // 64 samples
            .ring_capacity_expo = 8,   // 256 batches
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        .timeout_us = 1000000
    };
    
    err = map_init(&temp_converter, &map_config);
    if (err != Bp_EC_OK) {
        printf("Failed to initialize map filter: %d\n", err);
        csvsource_destroy(&csv_source);
        return 1;
    }
    
    // 3. Create output buffer for processed data
    Batch_buff_t output_buffer;
    BatchBuffer_config buffer_config = {
        .dtype = DTYPE_FLOAT,
        .batch_capacity_expo = 6,
        .ring_capacity_expo = 8,
        .overflow_behaviour = OVERFLOW_BLOCK
    };
    
    err = bb_init(&output_buffer, "output", buffer_config);
    if (err != Bp_EC_OK) {
        printf("Failed to initialize output buffer: %d\n", err);
        map_destroy(&temp_converter);
        csvsource_destroy(&csv_source);
        return 1;
    }
    
    // 4. Connect filters
    err = filt_sink_connect(&csv_source.base, 0, &temp_converter.base.input_buffers[0]);
    if (err != Bp_EC_OK) {
        printf("Failed to connect CSV source to map: %d\n", err);
        goto cleanup;
    }
    
    err = filt_sink_connect(&temp_converter.base, 0, &output_buffer);
    if (err != Bp_EC_OK) {
        printf("Failed to connect map to output: %d\n", err);
        goto cleanup;
    }
    
    // 5. Start processing
    bb_start(&output_buffer);
    filt_start(&temp_converter.base);
    filt_start(&csv_source.base);
    
    printf("Processing CSV data...\n");
    
    // 6. Read and display results
    int batch_count = 0;
    while (1) {
        Batch_t* batch = bb_get_tail(&output_buffer, 1000000, &err);
        if (!batch) {
            if (err == Bp_EC_TIMEOUT) {
                continue;
            }
            break;
        }
        
        if (batch->ec == Bp_EC_COMPLETE) {
            printf("Processing complete!\n");
            bb_del_tail(&output_buffer);
            break;
        }
        
        float* data = (float*)batch->data;
        printf("Batch %d: %zu samples, first timestamp: %lu ns\n", 
               ++batch_count, batch->tail, batch->t_ns);
        
        // Display first few samples
        size_t n_display = (batch->tail < 3) ? batch->tail : 3;
        for (size_t i = 0; i < n_display; i++) {
            printf("  Sample %zu: Temp=%.1f°F, Humidity=%.1f%%, Pressure=%.2f hPa\n",
                   i, data[i*3], data[i*3+1], data[i*3+2]);
        }
        
        bb_del_tail(&output_buffer);
        
        // Get filter statistics
        if (batch_count % 10 == 0) {
            char desc[1024];
            csv_source.base.ops.describe(&csv_source.base, desc, sizeof(desc));
            printf("\nFilter status:\n%s\n", desc);
        }
    }
    
    // 7. Stop and cleanup
    filt_stop(&csv_source.base);
    filt_stop(&temp_converter.base);
    bb_stop(&output_buffer);
    
cleanup:
    bb_deinit(&output_buffer);
    map_destroy(&temp_converter);
    csvsource_destroy(&csv_source);
    
    return 0;
}