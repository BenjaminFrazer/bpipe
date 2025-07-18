#ifndef TEE_H
#define TEE_H

#include "bperr.h"
#include "core.h"

typedef struct _Tee_config_t {
    const char* name;                          // Filter name for debugging
    BatchBuffer_config buff_config;            // Input buffer configuration
    size_t n_outputs;                          // Number of output sinks (2-MAX_SINKS)
    BatchBuffer_config* output_configs;        // Array of output buffer configs
    long timeout_us;                           // Timeout for buffer operations
    bool copy_data;                            // true=deep copy, false=reference only
} Tee_config_t;

typedef struct _Tee_filt_t {
    Filter_t base;
    bool copy_data;
    size_t n_outputs;
    size_t successful_writes[MAX_SINKS];       // Track successful writes per output
} Tee_filt_t;

Bp_EC tee_init(Tee_filt_t* tee, Tee_config_t config);

#endif /* TEE_H */