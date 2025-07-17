#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core.h"
#include "map.h"
#include "batch_buffer.h"

/* Simple example showing filter operations in action */

static Bp_EC identity_map(char* output, char* input, size_t n_samples)
{
    memcpy(output, input, n_samples * sizeof(uint32_t));
    return Bp_EC_OK;
}

int main(void)
{
    printf("Testing Filter Operations Interface\n");
    printf("===================================\n\n");
    
    // Create a map filter
    Map_filt_t map_filter;
    Map_config_t config = {
        .buff_config = {
            .dtype = DTYPE_U32,
            .batch_capacity_expo = 4,   // 16 samples per batch
            .ring_capacity_expo = 2     // 4 batches in ring
        },
        .map_fcn = identity_map
    };
    
    Bp_EC result = map_init(&map_filter, config);
    if (result != Bp_EC_OK) {
        printf("Failed to initialize map filter: %d\n", result);
        return 1;
    }
    
    // Test filter operations
    printf("1. Testing filt_describe():\n");
    printf("   %s\n\n", filt_describe(&map_filter.base));
    
    printf("2. Testing filt_get_stats():\n");
    Filt_metrics stats;
    filt_get_stats(&map_filter.base, &stats);
    printf("   Batches processed: %zu\n\n", stats.n_batches);
    
    printf("3. Testing filt_get_health():\n");
    FilterHealth_t health;
    result = filt_get_health(&map_filter.base, &health);
    printf("   Health status: %s\n\n", 
           (health == FILT_HEALTH_OK) ? "OK" : 
           (health == FILT_HEALTH_DEGRADED) ? "DEGRADED" : "FAILED");
    
    printf("4. Testing filt_flush():\n");
    result = filt_flush(&map_filter.base);
    printf("   Flush result: %s\n\n", (result == Bp_EC_OK) ? "Success" : "Failed");
    
    printf("5. Testing filt_reset():\n");
    result = filt_reset(&map_filter.base);
    printf("   Reset result: %s\n\n", (result == Bp_EC_OK) ? "Success" : "Failed");
    
    printf("6. Testing filt_get_backlog():\n");
    size_t backlog = filt_get_backlog(&map_filter.base);
    printf("   Backlog: %zu samples\n\n", backlog);
    
    printf("7. Testing filt_reconfigure():\n");
    result = filt_reconfigure(&map_filter.base, NULL);
    printf("   Reconfigure result: %s\n\n", 
           (result == Bp_EC_NOT_IMPLEMENTED) ? "Not implemented (expected)" : "Unexpected result");
    
    printf("8. Testing filt_dump_state():\n");
    printf("   Filter state dump:\n");
    filt_dump_state(&map_filter.base, stdout);
    printf("\n");
    
    // Clean up
    result = filt_deinit(&map_filter.base);
    if (result != Bp_EC_OK) {
        printf("Failed to deinitialize filter: %d\n", result);
        return 1;
    }
    
    printf("All filter operations tests completed successfully!\n");
    return 0;
}