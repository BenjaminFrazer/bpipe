#include "tee.h"
#include <string.h>

static void* tee_worker(void* arg) {
    Tee_filt_t* tee = (Tee_filt_t*)arg;
    Filter_t* f = &tee->base;
    
    while (f->running) {
        // Get input batch
        Bp_EC err;
        Batch_t* input = bb_get_tail(&f->input_buffers[0], f->timeout_us, &err);
        if (!input) {
            if (err == Bp_EC_TIMEOUT) continue;
            // Handle completion or error
            break;
        }
        
        // Priority copy to output 0 first (hot path)
        for (size_t i = 0; i < tee->n_outputs && i < f->n_sinks; i++) {
            if (!f->sinks[i]) continue;
            
            // Get output buffer (respects buffer's own timeout/overflow settings)
            Batch_t* output = bb_get_head(f->sinks[i]);
            if (!output) {
                // Buffer's overflow behavior determines what happens
                // OVERFLOW_BLOCK: we'll wait
                // OVERFLOW_DROP_TAIL: buffer will handle dropping
                continue;
            }
            
            // Check if output buffer is large enough
            size_t output_batch_size = bb_batch_size(f->sinks[i]);
            if (input->head > output_batch_size) {
                // For now, just copy what fits - proper handling would require
                // maintaining state for partial batches
                output->head = output_batch_size;
            } else {
                output->head = input->head;
            }
            
            // Deep copy data
            size_t data_width = bb_getdatawidth(f->input_buffers[0].dtype);
            size_t data_size = output->head * data_width;
            memcpy(output->data, input->data, data_size);
            
            // Copy metadata
            output->tail = 0;  // Always start from beginning of output batch
            output->t_ns = input->t_ns;
            output->period_ns = input->period_ns;
            output->batch_id = input->batch_id;
            
            err = bb_submit(f->sinks[i], f->timeout_us);
            if (err == Bp_EC_OK) {
                tee->successful_writes[i]++;
            }
        }
        
        // Remove input batch after distribution
        bb_del_tail(&f->input_buffers[0]);
        
        // Update metrics
        f->metrics.n_batches++;
        f->metrics.samples_processed += input->head;
    }
    
    // Shutdown: wait for all outputs to flush
    // Note: bb_flush doesn't exist, but buffers will be flushed 
    // when downstream filters consume remaining data
    
    return NULL;
}

Bp_EC tee_init(Tee_filt_t* tee, Tee_config_t config) {
    // Validate inputs
    if (!tee || !config.output_configs) {
        return Bp_EC_NULL_POINTER;
    }
    
    if (config.n_outputs < 2 || config.n_outputs > MAX_SINKS) {
        return Bp_EC_INVALID_CONFIG;
    }
    
    // Validate all outputs have same dtype
    SampleDtype_t expected_dtype = config.output_configs[0].dtype;
    for (size_t i = 1; i < config.n_outputs; i++) {
        if (config.output_configs[i].dtype != expected_dtype) {
            return Bp_EC_TYPE_MISMATCH;
        }
    }
    
    // Initialize tee-specific fields
    tee->copy_data = config.copy_data;
    tee->n_outputs = config.n_outputs;
    tee->input_position = 0;
    memset(tee->successful_writes, 0, sizeof(tee->successful_writes));
    memset(tee->output_batches, 0, sizeof(tee->output_batches));
    
    // Initialize base filter
    Core_filt_config_t core_config = {
        .name = config.name,
        .filt_type = FILT_T_SIMO_TEE,
        .size = sizeof(Tee_filt_t),
        .n_inputs = 1,
        .max_supported_sinks = config.n_outputs,
        .buff_config = config.buff_config,  // Use provided input buffer config
        .timeout_us = config.timeout_us,
        .worker = tee_worker
    };
    
    return filt_init(&tee->base, core_config);
}