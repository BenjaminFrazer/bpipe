#include "resampler.h"
#include <math.h>
#include <string.h>

/* Helper to convert Hz to period in nanoseconds */
static unsigned long hz_to_period_ns(unsigned long hz) {
    return 1000000000UL / hz;
}

/* Helper to allocate and initialize input states */
static Bp_EC allocate_input_states(BpZOHResampler_t* resampler, size_t n_inputs, 
                                  size_t data_width, size_t max_buffer) {
    resampler->input_states = calloc(n_inputs, sizeof(BpInputState_t));
    if (!resampler->input_states) {
        return Bp_EC_MALLOC_FAIL;
    }
    
    /* Initialize each input state */
    for (size_t i = 0; i < n_inputs; i++) {
        BpInputState_t* state = &resampler->input_states[i];
        
        /* Allocate buffer for last values */
        state->last_values = calloc(max_buffer, data_width);
        if (!state->last_values) {
            /* Clean up previously allocated buffers */
            for (size_t j = 0; j < i; j++) {
                free(resampler->input_states[j].last_values);
            }
            free(resampler->input_states);
            resampler->input_states = NULL;
            return Bp_EC_MALLOC_FAIL;
        }
        
        state->last_values_size = max_buffer;
        state->has_data = false;
        state->underrun = false;
        state->last_t_ns = 0;
        state->last_period_ns = 0;
        state->last_batch_id = 0;
        state->samples_processed = 0;
        state->underrun_count = 0;
        state->discontinuity_count = 0;
    }
    
    return Bp_EC_OK;
}

/* Main initialization function */
Bp_EC BpZOHResampler_Init(BpZOHResampler_t* resampler, 
                         const BpZOHResamplerConfig* config) {
    if (!resampler || !config) {
        return Bp_EC_NULL_FILTER;
    }
    
    /* Validate configuration */
    if (config->output_period_ns == 0) {
        return Bp_EC_INVALID_CONFIG;
    }
    
    if (config->base_config.number_of_input_filters < 1 || 
        config->base_config.number_of_input_filters > MAX_SOURCES) {
        return Bp_EC_INVALID_CONFIG;
    }
    
    if (config->max_input_buffer < 1) {
        return Bp_EC_INVALID_CONFIG;
    }
    
    /* Initialize base filter with transform */
    BpFilterConfig base_config = config->base_config;
    base_config.transform = BpZOHResamplerTransform;
    
    Bp_EC ec = BpFilter_Init(&resampler->base, &base_config);
    if (ec != Bp_EC_OK) {
        return ec;
    }
    
    /* Store configuration */
    resampler->output_period_ns = config->output_period_ns;
    resampler->n_inputs = config->base_config.number_of_input_filters;
    resampler->drop_on_underrun = config->drop_on_underrun;
    resampler->method = BP_RESAMPLE_ZOH;
    
    /* Initialize timing */
    resampler->next_output_t_ns = 0;
    resampler->start_t_ns = 0;
    resampler->output_batch_id = 0;
    resampler->started = false;
    
    /* Allocate input states */
    ec = allocate_input_states(resampler, resampler->n_inputs, 
                              resampler->base.data_width, 
                              config->max_input_buffer);
    if (ec != Bp_EC_OK) {
        BpFilter_Deinit(&resampler->base);
        return ec;
    }
    
    /* Allocate temporary output buffer - needs space for interleaved output */
    resampler->temp_buffer_size = base_config.batch_size * resampler->n_inputs * resampler->base.data_width;
    resampler->temp_output_buffer = malloc(resampler->temp_buffer_size);
    if (!resampler->temp_output_buffer) {
        /* Clean up */
        for (size_t i = 0; i < resampler->n_inputs; i++) {
            free(resampler->input_states[i].last_values);
        }
        free(resampler->input_states);
        BpFilter_Deinit(&resampler->base);
        return Bp_EC_MALLOC_FAIL;
    }
    
    return Bp_EC_OK;
}

/* Simplified initialization */
Bp_EC BpZOHResampler_InitSimple(BpZOHResampler_t* resampler,
                               unsigned long output_rate_hz,
                               size_t n_inputs,
                               SampleDtype_t dtype) {
    BpZOHResamplerConfig config = BP_ZOH_RESAMPLER_CONFIG_DEFAULT;
    
    config.output_period_ns = hz_to_period_ns(output_rate_hz);
    config.base_config.number_of_input_filters = n_inputs;
    config.base_config.dtype = dtype;
    
    return BpZOHResampler_Init(resampler, &config);
}

/* Cleanup function */
Bp_EC BpZOHResampler_Deinit(BpZOHResampler_t* resampler) {
    if (!resampler) {
        return Bp_EC_NULL_FILTER;
    }
    
    /* Free input states */
    if (resampler->input_states) {
        for (size_t i = 0; i < resampler->n_inputs; i++) {
            free(resampler->input_states[i].last_values);
        }
        free(resampler->input_states);
        resampler->input_states = NULL;
    }
    
    /* Free temporary buffer */
    free(resampler->temp_output_buffer);
    resampler->temp_output_buffer = NULL;
    
    /* Deinit base filter */
    return BpFilter_Deinit(&resampler->base);
}

/* Process samples from a single input batch */
static void process_input_batch(BpZOHResampler_t* resampler, size_t input_idx,
                               Bp_Batch_t* batch, long long current_t_ns) {
    BpInputState_t* state = &resampler->input_states[input_idx];
    
    /* Check for discontinuity */
    if (state->has_data && state->last_batch_id + 1 != batch->batch_id) {
        state->discontinuity_count++;
    }
    
    /* Update state tracking */
    state->last_batch_id = batch->batch_id;
    state->last_period_ns = batch->period_ns;
    
    /* Process samples in the batch */
    size_t n_samples = batch->tail - batch->head;
    if (n_samples == 0) {
        return;
    }
    
    /* For ZOH, we only need the most recent sample(s) */
    size_t samples_to_copy = (n_samples < state->last_values_size) ? 
                            n_samples : state->last_values_size;
    
    /* Copy the most recent samples */
    size_t offset = batch->head + n_samples - samples_to_copy;
    memcpy(state->last_values, 
           (char*)batch->data + offset * resampler->base.data_width,
           samples_to_copy * resampler->base.data_width);
    
    /* Update timing based on batch type */
    if (batch->period_ns > 0) {
        /* Regular sampling - calculate time of last sample */
        state->last_t_ns = batch->t_ns + (n_samples - 1) * batch->period_ns;
    } else {
        /* Irregular sampling - use batch timestamp */
        state->last_t_ns = batch->t_ns;
    }
    
    state->has_data = true;
    state->underrun = false;
    state->samples_processed += n_samples;
}

/* Check if an input has valid data for the given output time */
static bool input_has_valid_data(BpInputState_t* state, long long output_t_ns,
                                bool drop_on_underrun) {
    if (!state->has_data) {
        return false;
    }
    
    if (drop_on_underrun && state->last_t_ns < output_t_ns) {
        /* In drop mode, we need data that's current */
        return false;
    }
    
    /* In hold mode, any past data is valid */
    return true;
}

/* Copy data from input state to output based on dtype */
static void copy_sample_by_dtype(void* dst, const void* src, size_t offset,
                                SampleDtype_t dtype) {
    switch (dtype) {
        case DTYPE_FLOAT:
            ((float*)dst)[offset] = ((float*)src)[0];
            break;
        case DTYPE_INT:
            ((int*)dst)[offset] = ((int*)src)[0];
            break;
        case DTYPE_UNSIGNED:
            ((unsigned*)dst)[offset] = ((unsigned*)src)[0];
            break;
        default:
            /* Should not happen if validation is correct */
            break;
    }
}

/* Main transform function */
void BpZOHResamplerTransform(Bp_Filter_t* filter, Bp_Batch_t** input_batches,
                            int n_inputs, Bp_Batch_t* const* output_batches,
                            int n_outputs) {
    BpZOHResampler_t* resampler = (BpZOHResampler_t*)filter;
    Bp_Batch_t* output = output_batches[0];
    
    /* Validate inputs match configuration */
    if (n_inputs != (int)resampler->n_inputs) {
        SET_FILTER_ERROR(filter, Bp_EC_INVALID_CONFIG, 
                        "Input count mismatch");
        return;
    }
    
    /* Get current time range from inputs */
    long long current_t_ns = 0;
    long long max_t_ns = 0;
    bool found_time = false;
    
    for (int i = 0; i < n_inputs; i++) {
        Bp_Batch_t* batch = input_batches[i];
        if (batch->tail > batch->head) {
            if (!found_time) {
                current_t_ns = batch->t_ns;
                found_time = true;
            }
            
            /* Calculate end time of this batch */
            long long batch_end_t_ns;
            if (batch->period_ns > 0) {
                /* Regular sampling */
                size_t n_samples = batch->tail - batch->head;
                batch_end_t_ns = batch->t_ns + (n_samples - 1) * batch->period_ns;
            } else {
                /* Irregular sampling - just use batch timestamp */
                batch_end_t_ns = batch->t_ns;
            }
            
            if (batch_end_t_ns > max_t_ns) {
                max_t_ns = batch_end_t_ns;
            }
        }
    }
    
    /* If no inputs have data, nothing to do */
    if (!found_time) {
        output->head = 0;
        output->tail = 0;
        output->ec = Bp_EC_OK;
        return;
    }
    
    /* Initialize output timing on first run */
    if (!resampler->started) {
        resampler->start_t_ns = current_t_ns;
        resampler->next_output_t_ns = current_t_ns;
        resampler->started = true;
    }
    
    /* Process all available input data */
    for (int i = 0; i < n_inputs; i++) {
        Bp_Batch_t* batch = input_batches[i];
        if (batch->tail > batch->head) {
            process_input_batch(resampler, i, batch, max_t_ns);
        }
    }
    
    /* Generate output samples at fixed rate */
    size_t output_count = 0;
    /* For interleaved output, max samples is capacity divided by number of inputs */
    size_t max_output_samples = output->capacity / resampler->n_inputs;
    
    /* Also limit by temp buffer size */
    size_t max_temp_samples = resampler->temp_buffer_size / 
                             (resampler->n_inputs * filter->data_width);
    if (max_output_samples > max_temp_samples) {
        max_output_samples = max_temp_samples;
    }
    
    while (resampler->next_output_t_ns <= max_t_ns && 
           output_count < max_output_samples) {
        
        /* Check if all inputs have valid data */
        bool all_valid = true;
        for (size_t i = 0; i < resampler->n_inputs; i++) {
            if (!input_has_valid_data(&resampler->input_states[i], 
                                     resampler->next_output_t_ns,
                                     resampler->drop_on_underrun)) {
                all_valid = false;
                resampler->input_states[i].underrun = true;
                resampler->input_states[i].underrun_count++;
            }
        }
        
        /* Skip this output sample if in drop mode and missing data */
        if (!all_valid && resampler->drop_on_underrun) {
            resampler->next_output_t_ns += resampler->output_period_ns;
            continue;
        }
        
        /* Copy data from each input using ZOH */
        for (size_t i = 0; i < resampler->n_inputs; i++) {
            BpInputState_t* state = &resampler->input_states[i];
            
            if (state->has_data) {
                /* Copy the held value */
                copy_sample_by_dtype(
                    resampler->temp_output_buffer,
                    state->last_values,
                    output_count * resampler->n_inputs + i,
                    filter->dtype
                );
            } else {
                /* No data yet - output zeros */
                memset((char*)resampler->temp_output_buffer + 
                       (output_count * resampler->n_inputs + i) * filter->data_width,
                       0, filter->data_width);
            }
        }
        
        output_count++;
        resampler->next_output_t_ns += resampler->output_period_ns;
    }
    
    /* Copy generated samples to output batch */
    if (output_count > 0) {
        memcpy(output->data, resampler->temp_output_buffer,
               output_count * resampler->n_inputs * filter->data_width);
        
        output->head = 0;
        output->tail = output_count * resampler->n_inputs;
        output->t_ns = resampler->next_output_t_ns - 
                       output_count * resampler->output_period_ns;
        output->period_ns = resampler->output_period_ns;
        output->batch_id = resampler->output_batch_id++;
        output->dtype = filter->dtype;
        output->ec = Bp_EC_OK;
    } else {
        output->head = 0;
        output->tail = 0;
        output->ec = Bp_EC_OK;
    }
}

/* Get statistics for an input */
Bp_EC BpZOHResampler_GetInputStats(const BpZOHResampler_t* resampler,
                                  size_t input_idx,
                                  BpResamplerInputStats_t* stats) {
    if (!resampler || !stats) {
        return Bp_EC_NULL_FILTER;
    }
    
    if (input_idx >= resampler->n_inputs) {
        return Bp_EC_INVALID_CONFIG;
    }
    
    const BpInputState_t* state = &resampler->input_states[input_idx];
    
    stats->samples_processed = state->samples_processed;
    stats->underrun_count = state->underrun_count;
    stats->discontinuity_count = state->discontinuity_count;
    
    /* Calculate average input rate if we have timing info */
    if (state->last_period_ns > 0) {
        stats->avg_input_rate_hz = 1e9 / state->last_period_ns;
    } else {
        stats->avg_input_rate_hz = 0.0;
    }
    
    /* Calculate underrun percentage */
    if (resampler->output_batch_id > 0) {
        stats->underrun_percentage = 100.0 * state->underrun_count / 
                                   resampler->output_batch_id;
    } else {
        stats->underrun_percentage = 0.0;
    }
    
    return Bp_EC_OK;
}

/* Reset resampler state */
Bp_EC BpZOHResampler_Reset(BpZOHResampler_t* resampler) {
    if (!resampler) {
        return Bp_EC_NULL_FILTER;
    }
    
    /* Reset timing */
    resampler->next_output_t_ns = 0;
    resampler->start_t_ns = 0;
    resampler->output_batch_id = 0;
    resampler->started = false;
    
    /* Reset all input states */
    for (size_t i = 0; i < resampler->n_inputs; i++) {
        BpInputState_t* state = &resampler->input_states[i];
        
        state->has_data = false;
        state->underrun = false;
        state->last_t_ns = 0;
        state->last_period_ns = 0;
        state->last_batch_id = 0;
        state->samples_processed = 0;
        state->underrun_count = 0;
        state->discontinuity_count = 0;
        
        /* Clear last values buffer */
        memset(state->last_values, 0, 
               state->last_values_size * resampler->base.data_width);
    }
    
    return Bp_EC_OK;
}