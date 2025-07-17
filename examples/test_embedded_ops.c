#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "core.h"
#include "map.h"

Bp_EC test_map_function(char* out, char* in, size_t n) {
    uint32_t* output = (uint32_t*)out;
    uint32_t* input = (uint32_t*)in;
    
    for (size_t i = 0; i < n; i++) {
        output[i] = input[i] * 2;  // Double the input
    }
    
    return Bp_EC_OK;
}

int main() {
    printf("Testing embedded FilterOps implementation...\n");
    
    // Create a Map filter
    Map_filt_t map_filter;
    Map_config_t config = {
        .buff_config = {
            .dtype = DTYPE_U32,
            .batch_capacity_expo = 4,    // 16 samples per batch
            .ring_capacity_expo = 4,     // 16 batches in ring
            .overflow_behaviour = OVERFLOW_BLOCK
        },
        .map_fcn = test_map_function
    };
    
    Bp_EC rc = map_init(&map_filter, config);
    if (rc != Bp_EC_OK) {
        printf("Failed to initialize map filter: %d\n", rc);
        return 1;
    }
    
    // Test the embedded operations
    printf("Testing filter operations...\n");
    
    // Test describe operation
    char describe_buffer[1024];
    rc = filt_describe(&map_filter.base, describe_buffer, sizeof(describe_buffer));
    if (rc == Bp_EC_OK) {
        printf("Describe operation result:\n%s\n", describe_buffer);
    } else {
        printf("Describe operation failed: %d\n", rc);
    }
    
    // Test get_health operation
    FilterHealth_t health = filt_get_health(&map_filter.base);
    printf("Filter health: %d (0=Healthy, 1=Degraded, 2=Failed, 3=Unknown)\n", health);
    
    // Test get_backlog operation
    size_t backlog = filt_get_backlog(&map_filter.base);
    printf("Filter backlog: %zu\n", backlog);
    
    // Test dump_state operation
    char state_buffer[1024];
    rc = filt_dump_state(&map_filter.base, state_buffer, sizeof(state_buffer));
    if (rc == Bp_EC_OK) {
        printf("Dump state operation result:\n%s\n", state_buffer);
    } else {
        printf("Dump state operation failed: %d\n", rc);
    }
    
    // Test get_stats operation
    Map_stats_t stats;
    rc = filt_get_stats(&map_filter.base, &stats);
    if (rc == Bp_EC_OK) {
        printf("Stats: batches=%zu, input_backlog=%zu, output_backlog=%zu\n",
               stats.base_stats.n_batches, stats.input_backlog, stats.output_backlog);
    } else {
        printf("Get stats operation failed: %d\n", rc);
    }
    
    // Test flush operation
    rc = filt_flush(&map_filter.base);
    printf("Flush operation result: %d\n", rc);
    
    // Test reset operation
    rc = filt_reset(&map_filter.base);
    printf("Reset operation result: %d\n", rc);
    
    // Test validation
    rc = filt_validate_connection(&map_filter.base, 0);
    printf("Validate connection result: %d\n", rc);
    
    // Test invalid connection
    rc = filt_validate_connection(&map_filter.base, 99);
    printf("Validate invalid connection result: %d\n", rc);
    
    // Test reconfigure (should return NOT_IMPLEMENTED)
    rc = filt_reconfigure(&map_filter.base, NULL);
    printf("Reconfigure operation result: %d\n", rc);
    
    // Test error handling and recovery
    rc = filt_handle_error(&map_filter.base, Bp_EC_TIMEOUT);
    printf("Handle error operation result: %d\n", rc);
    
    health = filt_get_health(&map_filter.base);
    printf("Filter health after error: %d\n", health);
    
    rc = filt_recover(&map_filter.base);
    printf("Recover operation result: %d\n", rc);
    
    health = filt_get_health(&map_filter.base);
    printf("Filter health after recovery: %d\n", health);
    
    // Cleanup
    rc = filt_deinit(&map_filter.base);
    if (rc == Bp_EC_OK) {
        printf("Filter deinitialized successfully\n");
    } else {
        printf("Failed to deinitialize filter: %d\n", rc);
    }
    
    printf("Test completed successfully!\n");
    return 0;
}