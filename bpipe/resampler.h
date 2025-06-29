#ifndef BPIPE_RESAMPLER_H
#define BPIPE_RESAMPLER_H

#include "core.h"

/* Resampling methods */
typedef enum {
    BP_RESAMPLE_ZOH = 0,        /* Zero-order hold (sample and hold) */
    BP_RESAMPLE_LINEAR,         /* Linear interpolation (future) */
    BP_RESAMPLE_CUBIC,          /* Cubic interpolation (future) */
} BpResampleMethod_t;

/* Per-input state tracking */
typedef struct {
    /* Last known values buffer */
    void* last_values;          /* Buffer for last known samples */
    size_t last_values_size;    /* Number of samples in buffer */
    
    /* Timing information */
    long long last_t_ns;        /* Timestamp of last processed sample */
    unsigned last_period_ns;    /* Last known input period (0 = irregular) */
    size_t last_batch_id;       /* For discontinuity detection */
    
    /* State flags */
    bool has_data;              /* Has received at least one sample */
    bool underrun;              /* Currently in underrun state */
    
    /* Statistics */
    uint64_t samples_processed;
    uint64_t underrun_count;
    uint64_t discontinuity_count;
} BpInputState_t;

/* Configuration structure for ZOH resampler */
typedef struct {
    BpFilterConfig base_config;      /* Standard filter configuration */
    unsigned long output_period_ns;  /* Desired output sample period */
    bool drop_on_underrun;          /* Drop output or repeat last value */
    size_t max_input_buffer;        /* Max samples to store per input (usually 1 for ZOH) */
} BpZOHResamplerConfig;

/* Default configuration */
#define BP_ZOH_RESAMPLER_CONFIG_DEFAULT { \
    .base_config = BP_FILTER_CONFIG_DEFAULT, \
    .output_period_ns = 1000000, \
    .drop_on_underrun = false, \
    .max_input_buffer = 1 \
}

/* Zero-order hold resampler filter */
typedef struct {
    Bp_Filter_t base;               /* Base filter (must be first) */
    
    /* Configuration */
    unsigned long output_period_ns;  /* Target output sample period */
    size_t n_inputs;                /* Number of input streams */
    bool drop_on_underrun;          /* Drop output or repeat last value */
    BpResampleMethod_t method;      /* Resampling method (ZOH for now) */
    
    /* Per-input state - dynamically sized based on n_inputs */
    BpInputState_t* input_states;   /* Array of input states */
    
    /* Output timing control */
    long long next_output_t_ns;     /* Next output sample time */
    long long start_t_ns;           /* First output sample time */
    size_t output_batch_id;         /* Output batch sequence number */
    bool started;                   /* Has output timing been initialized */
    
    /* Working buffers */
    void* temp_output_buffer;       /* Temporary buffer for output batch */
    size_t temp_buffer_size;        /* Size of temporary buffer */
} BpZOHResampler_t;

/* Initialization functions */
Bp_EC BpZOHResampler_Init(
    BpZOHResampler_t* resampler,
    const BpZOHResamplerConfig* config
);

/* Simplified initialization for common cases */
Bp_EC BpZOHResampler_InitSimple(
    BpZOHResampler_t* resampler,
    unsigned long output_rate_hz,   /* Output rate in Hz */
    size_t n_inputs,                /* Number of inputs to synchronize */
    SampleDtype_t dtype             /* Data type for all streams */
);

/* Cleanup function */
Bp_EC BpZOHResampler_Deinit(BpZOHResampler_t* resampler);

/* Transform function */
void BpZOHResamplerTransform(
    Bp_Filter_t* filter,
    Bp_Batch_t** input_batches,
    int n_inputs,
    Bp_Batch_t* const* output_batches,
    int n_outputs
);

/* Utility functions */

/* Get timing statistics for an input */
typedef struct {
    uint64_t samples_processed;
    uint64_t underrun_count;
    uint64_t discontinuity_count;
    double avg_input_rate_hz;
    double underrun_percentage;
} BpResamplerInputStats_t;

Bp_EC BpZOHResampler_GetInputStats(
    const BpZOHResampler_t* resampler,
    size_t input_idx,
    BpResamplerInputStats_t* stats
);

/* Reset resampler state (useful for testing) */
Bp_EC BpZOHResampler_Reset(BpZOHResampler_t* resampler);

#endif /* BPIPE_RESAMPLER_H */