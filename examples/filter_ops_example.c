#include "filter_ops_example.h"
#include <string.h>
#include <stdio.h>

/* ========== Simple Map Filter Implementation ========== */

static void* simple_map_worker(void* arg)
{
    SimpleMapFilter_t* f = (SimpleMapFilter_t*)arg;
    Batch_t *input, *output;
    Bp_EC err;
    
    while (f->base.running) {
        input = bb_get_tail(f->base.input_buffers[0], f->base.timeout_us, &err);
        if (!input || err != Bp_EC_OK) break;
        
        output = bb_get_head(f->base.sinks[0]);
        if (!output) break;
        
        /* Simple element-wise processing */
        size_t n_samples = input->head - input->tail;
        err = f->map_func(
            input->data + input->tail * f->base.data_width,
            output->data + output->head * f->base.data_width,
            n_samples
        );
        
        if (err == Bp_EC_OK) {
            output->head += n_samples;
            input->tail += n_samples;
            f->samples_processed += n_samples;
        }
        
        /* Submit output if batch full */
        if (output->head >= bb_batch_size(f->base.sinks[0])) {
            bb_submit(f->base.sinks[0], f->base.timeout_us);
        }
        
        /* Delete input if consumed */
        if (input->tail >= input->head) {
            bb_del_tail(f->base.input_buffers[0]);
        }
    }
    
    return NULL;
}

static const char* simple_map_describe(const Filter_t* self)
{
    return "Simple element-wise transformation";
}

static void simple_map_get_stats(const Filter_t* self, Filt_metrics* stats)
{
    SimpleMapFilter_t* f = (SimpleMapFilter_t*)self;
    stats->n_batches = f->base.metrics.n_batches;
    /* Could add samples_processed to metrics */
}

Bp_EC simple_map_init(SimpleMapFilter_t* f, MapFunc_t map_func, BatchBuffer_config buff_config)
{
    /* Populate ops for this instance */
    f->base.ops.start = NULL;  /* Use default */
    f->base.ops.stop = NULL;   /* Use default */
    f->base.ops.deinit = NULL; /* Use default */
    f->base.ops.flush = NULL;  /* TODO: Could implement */
    f->base.ops.reset = NULL;  /* TODO: Could implement */
    f->base.ops.describe = simple_map_describe;
    f->base.ops.get_stats = simple_map_get_stats;
    f->base.ops.get_latency_ns = NULL;
    
    /* Set specific fields */
    f->map_func = map_func;
    f->samples_processed = 0;
    
    /* Initialize base filter */
    Core_filt_config_t config = {
        .name = "SIMPLE_MAP",
        .size = sizeof(SimpleMapFilter_t),
        .worker = simple_map_worker,
        .timeout_us = 10000,
        .max_supported_sinks = 1,
        .n_inputs = 1,
        .filt_type = FILT_T_MAP,
        .buff_config = buff_config
    };
    
    return filt_init((Filter_t*)f, config);
}

/* ========== Stateful Map Filter Implementation ========== */

static void* stateful_map_worker(void* arg)
{
    StatefulMapFilter_t* f = (StatefulMapFilter_t*)arg;
    Batch_t *input, *output;
    Bp_EC err;
    
    /* Initialize state once */
    if (f->init_state && f->state) {
        f->init_state(f->state, f->state_size);
    }
    
    while (f->base.running) {
        input = bb_get_tail(f->base.input_buffers[0], f->base.timeout_us, &err);
        if (!input || err != Bp_EC_OK) break;
        
        output = bb_get_head(f->base.sinks[0]);
        if (!output) break;
        
        /* Process with state */
        size_t n_samples = input->head - input->tail;
        err = f->map_func(
            input->data + input->tail * f->base.data_width,
            output->data + output->head * f->base.data_width,
            f->state,
            n_samples
        );
        
        if (err == Bp_EC_OK) {
            output->head += n_samples;
            input->tail += n_samples;
        }
        
        /* Submit/delete as before */
        if (output->head >= bb_batch_size(f->base.sinks[0])) {
            bb_submit(f->base.sinks[0], f->base.timeout_us);
        }
        if (input->tail >= input->head) {
            bb_del_tail(f->base.input_buffers[0]);
        }
    }
    
    return NULL;
}

static Bp_EC stateful_map_reset(Filter_t* self)
{
    StatefulMapFilter_t* f = (StatefulMapFilter_t*)self;
    if (f->init_state && f->state) {
        return f->init_state(f->state, f->state_size);
    }
    return Bp_EC_OK;
}

static const char* stateful_map_describe(const Filter_t* self)
{
    return "Stateful element-wise transformation with persistent state";
}

/* ========== Function Generator Implementation ========== */

static void* func_gen_worker(void* arg)
{
    FuncGenFilter_t* f = (FuncGenFilter_t*)arg;
    Batch_t *output;
    Bp_EC err;
    
    while (f->base.running) {
        output = bb_get_head(f->base.sinks[0]);
        if (!output) break;
        
        /* Generate samples */
        size_t batch_size = bb_batch_size(f->base.sinks[0]);
        err = f->generate_func(
            output->data,
            f->samples_generated,
            batch_size,
            f->sample_rate,
            f->generator_params
        );
        
        if (err == Bp_EC_OK) {
            output->head = batch_size;
            output->t_ns = (f->samples_generated * 1000000000LL) / (long long)f->sample_rate;
            output->period_ns = (unsigned)(1000000000.0 / f->sample_rate);
            
            f->samples_generated += batch_size;
            
            bb_submit(f->base.sinks[0], f->base.timeout_us);
        }
        
        /* Rate limiting could go here */
    }
    
    return NULL;
}

static size_t func_gen_get_latency(const Filter_t* self)
{
    /* Function generators have zero input latency */
    return 0;
}

/* ========== MIMO Synchronizer Implementation ========== */

static void* mimo_sync_worker(void* arg)
{
    MIMOSyncFilter_t* f = (MIMOSyncFilter_t*)arg;
    Batch_t* inputs[MAX_INPUTS];
    Batch_t* outputs[MAX_SINKS];
    Bp_EC err;
    
    while (f->base.running) {
        /* Collect inputs within sync window */
        bool all_ready = true;
        uint64_t min_timestamp = UINT64_MAX;
        uint64_t max_timestamp = 0;
        
        /* Check all inputs */
        for (int i = 0; i < f->base.n_input_buffers; i++) {
            inputs[i] = bb_get_tail(f->base.input_buffers[i], 0, &err);
            if (inputs[i] && err == Bp_EC_OK) {
                f->input_state[i].has_data = true;
                f->input_state[i].last_timestamp = inputs[i]->t_ns;
                
                if (inputs[i]->t_ns < min_timestamp) min_timestamp = inputs[i]->t_ns;
                if (inputs[i]->t_ns > max_timestamp) max_timestamp = inputs[i]->t_ns;
            } else {
                f->input_state[i].has_data = false;
                if (f->require_all_inputs) {
                    all_ready = false;
                }
            }
        }
        
        /* Check if we can sync */
        if (!all_ready || (max_timestamp - min_timestamp) > f->max_skew_ns) {
            /* Wait a bit or handle skew */
            usleep(1000);
            continue;
        }
        
        /* Get output buffers */
        for (int i = 0; i < f->base.n_sinks; i++) {
            if (f->base.sinks[i]) {
                outputs[i] = bb_get_head(f->base.sinks[i]);
            }
        }
        
        /* Process synchronized data */
        /* ... actual synchronization logic ... */
        
        /* Submit outputs and consume inputs */
        for (int i = 0; i < f->base.n_input_buffers; i++) {
            if (f->input_state[i].has_data) {
                bb_del_tail(f->base.input_buffers[i]);
            }
        }
        
        for (int i = 0; i < f->base.n_sinks; i++) {
            if (f->base.sinks[i] && outputs[i]) {
                bb_submit(f->base.sinks[i], f->base.timeout_us);
            }
        }
    }
    
    return NULL;
}

static void mimo_sync_get_stats(const Filter_t* self, Filt_metrics* stats)
{
    MIMOSyncFilter_t* f = (MIMOSyncFilter_t*)self;
    stats->n_batches = f->base.metrics.n_batches;
    /* Could add per-input dropped counts */
}

/* ========== Tee Filter Implementation ========== */

static void* tee_worker(void* arg)
{
    TeeFilter_t* f = (TeeFilter_t*)arg;
    Batch_t *input, *output;
    Bp_EC err;
    
    while (f->base.running) {
        input = bb_get_tail(f->base.input_buffers[0], f->base.timeout_us, &err);
        if (!input || err != Bp_EC_OK) break;
        
        switch (f->mode) {
            case TEE_MODE_DUPLICATE:
                /* Copy to all outputs */
                for (int i = 0; i < f->base.n_sinks; i++) {
                    if (f->base.sinks[i]) {
                        output = bb_get_head(f->base.sinks[i]);
                        if (output) {
                            memcpy(output->data, input->data, 
                                   bb_batch_size(f->base.sinks[i]) * f->base.data_width);
                            output->head = input->head;
                            output->t_ns = input->t_ns;
                            output->period_ns = input->period_ns;
                            bb_submit(f->base.sinks[i], 0);
                        }
                    }
                }
                break;
                
            case TEE_MODE_ROUND_ROBIN:
                /* Send to next output in sequence */
                if (f->base.sinks[f->next_output]) {
                    output = bb_get_head(f->base.sinks[f->next_output]);
                    if (output) {
                        memcpy(output->data, input->data,
                               bb_batch_size(f->base.sinks[f->next_output]) * f->base.data_width);
                        output->head = input->head;
                        bb_submit(f->base.sinks[f->next_output], 0);
                    }
                }
                f->next_output = (f->next_output + 1) % f->base.n_sinks;
                break;
                
            case TEE_MODE_CONDITIONAL:
                /* Route based on condition */
                for (int i = 0; i < f->base.n_sinks; i++) {
                    if (f->base.sinks[i] && 
                        f->route_condition && 
                        f->route_condition(input, i, f->routing_context)) {
                        output = bb_get_head(f->base.sinks[i]);
                        if (output) {
                            memcpy(output->data, input->data,
                                   bb_batch_size(f->base.sinks[i]) * f->base.data_width);
                            output->head = input->head;
                            bb_submit(f->base.sinks[i], 0);
                        }
                    }
                }
                break;
                
            default:
                break;
        }
        
        bb_del_tail(f->base.input_buffers[0]);
    }
    
    return NULL;
}

static const char* tee_describe(const Filter_t* self)
{
    TeeFilter_t* f = (TeeFilter_t*)self;
    switch (f->mode) {
        case TEE_MODE_DUPLICATE: return "Tee filter - duplicates input to all outputs";
        case TEE_MODE_ROUND_ROBIN: return "Tee filter - round-robin distribution";
        case TEE_MODE_LOAD_BALANCE: return "Tee filter - load-balanced distribution";
        case TEE_MODE_CONDITIONAL: return "Tee filter - conditional routing";
        default: return "Tee filter";
    }
}

/* ========== Example Initialization Functions ========== */

Bp_EC tee_init(TeeFilter_t* f, TeeMode_t mode, BatchBuffer_config buff_config)
{
    /* Set up ops */
    f->base.ops.start = NULL;
    f->base.ops.stop = NULL;
    f->base.ops.deinit = NULL;
    f->base.ops.flush = NULL;
    f->base.ops.reset = NULL;
    f->base.ops.describe = tee_describe;
    f->base.ops.get_stats = NULL;
    f->base.ops.get_latency_ns = NULL;
    
    /* Set tee-specific fields */
    f->mode = mode;
    f->next_output = 0;
    f->route_condition = NULL;
    f->routing_context = NULL;
    
    /* Initialize base */
    Core_filt_config_t config = {
        .name = "TEE",
        .size = sizeof(TeeFilter_t),
        .worker = tee_worker,
        .timeout_us = 10000,
        .max_supported_sinks = MAX_SINKS,  /* Tee supports many outputs */
        .n_inputs = 1,
        .filt_type = FILT_T_SIMO_TEE,
        .buff_config = buff_config
    };
    
    return filt_init((Filter_t*)f, config);
}