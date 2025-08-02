#ifndef FILTER_OPS_EXAMPLE_H
#define FILTER_OPS_EXAMPLE_H

#include "core.h"
#include "batch_buffer.h"

/* Filter operations that can be customized per instance */
typedef struct _FilterOps {
    /* Lifecycle - can be NULL to use defaults */
    Bp_EC (*start)(Filter_t* self);
    Bp_EC (*stop)(Filter_t* self);
    Bp_EC (*deinit)(Filter_t* self);
    
    /* Runtime operations */
    Bp_EC (*flush)(Filter_t* self);  /* Force processing of partial batches */
    Bp_EC (*reset)(Filter_t* self);  /* Reset internal state */
    
    /* Introspection */
    void (*get_stats)(const Filter_t* self, Filt_metrics* stats);
    const char* (*describe)(const Filter_t* self);
    size_t (*get_latency_ns)(const Filter_t* self);
} FilterOps;

/* Enhanced base filter with ops */
typedef struct _Filter_enhanced {
    /* Standard Filter_t fields */
    char name[32];
    size_t size;
    CORE_FILT_T filt_type;
    atomic_bool running;
    Worker_t *worker;
    Err_info worker_err_info;
    Filt_metrics metrics;
    unsigned long timeout_us;
    size_t max_supported_sinks;
    int n_input_buffers;
    size_t n_sink_buffers;
    int n_sinks;
    size_t data_width;
    pthread_t worker_thread;
    pthread_mutex_t filter_mutex;
    Batch_buff_t input_buffers[MAX_INPUTS];
    Batch_buff_t *sinks[MAX_SINKS];
    
    /* New: Operations table */
    FilterOps ops;
} Filter_enhanced_t;

/* ===== Simple Map Filter ===== */
typedef Bp_EC (*MapFunc_t)(const void* in, void* out, size_t n_samples);

typedef struct _SimpleMapFilter {
    Filter_enhanced_t base;
    MapFunc_t map_func;
    size_t samples_processed;
} SimpleMapFilter_t;

/* ===== Stateful Map Filter ===== */
typedef struct _StatefulMapFilter {
    Filter_enhanced_t base;
    void* state;
    size_t state_size;
    Bp_EC (*map_func)(const void* in, void* out, void* state, size_t n_samples);
    Bp_EC (*init_state)(void* state, size_t state_size);
    Bp_EC (*save_state)(const void* state, void* buffer, size_t* size);
    Bp_EC (*load_state)(void* state, const void* buffer, size_t size);
} StatefulMapFilter_t;

/* ===== Function Generator Filter ===== */
typedef struct _FuncGenFilter {
    Filter_enhanced_t base;
    double sample_rate;
    uint64_t samples_generated;
    Bp_EC (*generate_func)(void* out, uint64_t start_sample, size_t n_samples, double sample_rate, void* params);
    void* generator_params;
    size_t params_size;
} FuncGenFilter_t;

/* ===== MIMO Synchronizer Filter ===== */
typedef struct _MIMOSyncFilter {
    Filter_enhanced_t base;
    uint64_t sync_window_ns;
    uint64_t max_skew_ns;
    bool require_all_inputs;
    /* Per-input state tracking */
    struct {
        uint64_t last_timestamp;
        uint64_t samples_dropped;
        bool has_data;
    } input_state[MAX_INPUTS];
} MIMOSyncFilter_t;

/* ===== Tee Filter (1 to N distributor) ===== */
typedef enum {
    TEE_MODE_DUPLICATE,    /* Copy to all outputs */
    TEE_MODE_ROUND_ROBIN,  /* Distribute in round-robin */
    TEE_MODE_LOAD_BALANCE, /* Send to least loaded output */
    TEE_MODE_CONDITIONAL   /* Route based on condition */
} TeeMode_t;

typedef struct _TeeFilter {
    Filter_enhanced_t base;
    TeeMode_t mode;
    size_t next_output;  /* For round-robin */
    /* For conditional routing */
    bool (*route_condition)(const Batch_t* batch, size_t output_idx, void* context);
    void* routing_context;
} TeeFilter_t;

#endif /* FILTER_OPS_EXAMPLE_H */