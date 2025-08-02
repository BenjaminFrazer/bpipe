#include "passthrough.h"
#include "utils.h"
#include <stdatomic.h>
#include <unistd.h>
#include <string.h>

static void* passthrough_worker(void* arg)
{
    Passthrough_t* pt = (Passthrough_t*)arg;
    Bp_EC err = Bp_EC_OK;
    
    // Validate configuration
    BP_WORKER_ASSERT(&pt->base, pt->base.n_input_buffers == 1, Bp_EC_INVALID_CONFIG);
    BP_WORKER_ASSERT(&pt->base, pt->base.sinks[0] != NULL, Bp_EC_NO_SINK);
    
    // Main processing loop
    while (atomic_load(&pt->base.running)) {
        // Get input batch
        Batch_t* input = bb_get_tail(pt->base.input_buffers[0], 
                                     pt->base.timeout_us, &err);
        if (!input) {
            if (err == Bp_EC_TIMEOUT) continue;
            if (err == Bp_EC_STOPPED) break;
            break; // Real error
        }
        
        // Check for completion
        if (input->ec == Bp_EC_COMPLETE) {
            // Pass completion signal downstream
            Batch_t* output = bb_get_head(pt->base.sinks[0]);
            output->ec = Bp_EC_COMPLETE;
            output->head = 0;
            bb_submit(pt->base.sinks[0], pt->base.timeout_us);
            bb_del_tail(pt->base.input_buffers[0]);
            break;
        }
        
        // Validate input
        BP_WORKER_ASSERT(&pt->base, input->ec == Bp_EC_OK, input->ec);
        
        // Get output batch
        Batch_t* output = bb_get_head(pt->base.sinks[0]);
        
        // Copy batch metadata
        output->batch_id = input->batch_id;
        output->t_ns = input->t_ns;
        output->period_ns = input->period_ns;
        output->ec = input->ec;
        output->head = input->head;
        
        // Get data type and size
        size_t data_width = bb_getdatawidth(pt->base.input_buffers[0]->dtype);
        size_t n_samples = input->head;
        
        // Copy data
        memcpy(output->data, input->data, n_samples * data_width);
        
        // Submit output and delete input
        err = bb_submit(pt->base.sinks[0], pt->base.timeout_us);
        BP_WORKER_ASSERT(&pt->base, err == Bp_EC_OK, err);
        
        bb_del_tail(pt->base.input_buffers[0]);
        
        // Update metrics
        pt->base.metrics.samples_processed += n_samples;
        pt->base.metrics.n_batches++;
    }
    
    // Error handling
    if (err != Bp_EC_OK && err != Bp_EC_STOPPED) {
        pt->base.worker_err_info.ec = err;
        atomic_store(&pt->base.running, false);
    }
    
    return NULL;
}

static Bp_EC passthrough_describe(Filter_t* self, char* buffer, size_t size)
{
    snprintf(buffer, size, "Passthrough: %s\n"
             "  Type: Direct data passthrough\n"
             "  Batches processed: %zu\n"
             "  Samples processed: %zu\n",
             self->name,
             self->metrics.n_batches,
             self->metrics.samples_processed);
    return Bp_EC_OK;
}

Bp_EC passthrough_init(Passthrough_t* pt, Passthrough_config_t* config)
{
    if (pt == NULL) return Bp_EC_NULL_POINTER;
    if (config == NULL) return Bp_EC_NULL_POINTER;
    
    // Check if already initialized
    if (pt->base.filt_type != FILT_T_NDEF) {
        return Bp_EC_ALREADY_RUNNING;
    }
    
    // Validate config
    if (config->buff_config.dtype == DTYPE_NDEF || config->buff_config.dtype >= DTYPE_MAX) {
        return Bp_EC_INVALID_DTYPE;
    }
    
    // Build core config
    Core_filt_config_t core_config = {
        .name = config->name,
        .filt_type = FILT_T_MATCHED_PASSTHROUGH,
        .size = sizeof(Passthrough_t),
        .n_inputs = 1,  // Hardcoded: passthrough has 1 input
        .max_supported_sinks = 1,  // Hardcoded: passthrough has 1 output
        .buff_config = config->buff_config,
        .timeout_us = config->timeout_us,
        .worker = passthrough_worker
    };
    
    // Initialize base filter
    Bp_EC err = filt_init(&pt->base, core_config);
    if (err != Bp_EC_OK) return err;
    
    // Override operations
    pt->base.ops.describe = passthrough_describe;
    
    return Bp_EC_OK;
}