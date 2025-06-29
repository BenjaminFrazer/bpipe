#ifndef BPIPE_MATH_OPS_H
#define BPIPE_MATH_OPS_H

#include "core.h"

/* Configuration structures */

// Base configuration for all math operations
typedef struct {
    BpFilterConfig base_config;  // Standard filter configuration
    bool in_place;               // If true, reuse input buffer for output
    bool check_overflow;         // Enable overflow checking
    size_t simd_alignment;       // SIMD alignment hint (0 = auto)
} BpMathOpConfig;

// Single constant operation config
typedef struct {
    BpMathOpConfig math_config;
    float value;
} BpUnaryConstConfig;

// Multi-input operation config
typedef struct {
    BpMathOpConfig math_config;
    // Note: n_inputs stored in base_config.number_of_input_filters
} BpMultiOpConfig;

/* Specific config typedefs */
typedef BpUnaryConstConfig BpMultiplyConstConfig;
typedef BpMultiOpConfig BpMultiplyMultiConfig;

/* Operation structures */

// Multiply by constant
typedef struct {
    Bp_Filter_t base;
    float scale;
} BpMultiplyConst_t;

// Multiply multiple inputs
typedef struct {
    Bp_Filter_t base;
    // n_inputs accessed via base.n_sources
} BpMultiplyMulti_t;

/* Initialization functions */
Bp_EC BpMultiplyConst_Init(BpMultiplyConst_t* op,
                           const BpMultiplyConstConfig* config);
Bp_EC BpMultiplyMulti_Init(BpMultiplyMulti_t* op,
                           const BpMultiplyMultiConfig* config);

/* Transform functions */
void BpMultiplyConstTransform(Bp_Filter_t* filter, Bp_Batch_t** inputs,
                              int n_inputs, Bp_Batch_t* const* outputs,
                              int n_outputs);
void BpMultiplyMultiTransform(Bp_Filter_t* filter, Bp_Batch_t** inputs,
                              int n_inputs, Bp_Batch_t* const* outputs,
                              int n_outputs);

/* Default configurations */
#define BP_MATH_OP_CONFIG_DEFAULT                                   \
    {                                                               \
        .base_config = BP_FILTER_CONFIG_DEFAULT, .in_place = false, \
        .check_overflow = false, .simd_alignment = 0                \
    }

#endif /* BPIPE_MATH_OPS_H */