#define _GNU_SOURCE
#include "core.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


Bp_EC BpFilter_Deinit(Bp_Filter_t *filter)
{
    if (!filter) return Bp_EC_NOSPACE;

    // Make sure filter is stopped first
    if (filter->running) {
        Bp_Filter_Stop(filter);
    }

    // Destroy the filter mutex
    pthread_mutex_destroy(&filter->filter_mutex);

    // Deinitialize input buffers
    for (int i = 0; i < MAX_SOURCES; i++) {
        if (filter->input_buffers[i].data_ring != NULL) {
            Bp_BatchBuffer_Deinit(&filter->input_buffers[i]);
        }
    }

    return Bp_EC_OK;
}

const size_t _data_size_lut[] = {
    [DTYPE_FLOAT] = sizeof(float),
    [DTYPE_INT] = sizeof(int),
    [DTYPE_UNSIGNED] = sizeof(unsigned),
};

Bp_EC Bp_BatchBuffer_Init(Bp_BatchBuffer_t *buffer, size_t batch_size,
                          size_t number_of_batches)
{
    if (!buffer) return Bp_EC_NOSPACE;

    // Initialize buffer parameters
    buffer->head = 0;
    buffer->tail = 0;
    buffer->ring_capacity_expo = 0;
    buffer->batch_capacity_expo = 0;

    // Calculate ring capacity exponent (find log2 of number_of_batches)
    size_t temp = number_of_batches;
    while (temp > 1) {
        buffer->ring_capacity_expo++;
        temp >>= 1;
    }
    if (buffer->ring_capacity_expo > MAX_CAPACITY_EXPO) {
        buffer->ring_capacity_expo = MAX_CAPACITY_EXPO;
    }

    // Calculate batch capacity exponent (find log2 of batch_size)
    temp = batch_size;
    while (temp > 1) {
        buffer->batch_capacity_expo++;
        temp >>= 1;
    }
    if (buffer->batch_capacity_expo > MAX_CAPACITY_EXPO) {
        buffer->batch_capacity_expo = MAX_CAPACITY_EXPO;
    }

    // Initialize pthread synchronization objects
    if (pthread_mutex_init(&buffer->mutex, NULL) != 0) {
        return Bp_EC_MUTEX_INIT_FAIL;
    }

    if (pthread_cond_init(&buffer->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&buffer->mutex);
        return Bp_EC_COND_INIT_FAIL;
    }

    if (pthread_cond_init(&buffer->not_full, NULL) != 0) {
        pthread_mutex_destroy(&buffer->mutex);
        pthread_cond_destroy(&buffer->not_empty);
        return Bp_EC_COND_INIT_FAIL;
    }

    // Allocate ring buffers (will be done later by Bp_allocate_buffers)
    buffer->data_ring = NULL;
    buffer->batch_ring = NULL;
    buffer->stopped = false;

    return Bp_EC_OK;
}

Bp_EC Bp_BatchBuffer_Deinit(Bp_BatchBuffer_t *buffer)
{
    if (!buffer) return Bp_EC_NOSPACE;

    // Destroy pthread synchronization objects
    pthread_mutex_destroy(&buffer->mutex);
    pthread_cond_destroy(&buffer->not_empty);
    pthread_cond_destroy(&buffer->not_full);

    // Free allocated memory if it was allocated
    if (buffer->data_ring) {
        free(buffer->data_ring);
        buffer->data_ring = NULL;
    }

    if (buffer->batch_ring) {
        free(buffer->batch_ring);
        buffer->batch_ring = NULL;
    }

    // Reset buffer state
    buffer->head = 0;
    buffer->tail = 0;
    buffer->ring_capacity_expo = 0;
    buffer->batch_capacity_expo = 0;

    return Bp_EC_OK;
}

void BpBatchBuffer_stop(Bp_BatchBuffer_t* buffer)
{
    if (!buffer) return;
    
    pthread_mutex_lock(&buffer->mutex);
    buffer->stopped = true;
    // Wake up all waiting threads
    pthread_cond_broadcast(&buffer->not_empty);
    pthread_cond_broadcast(&buffer->not_full);
    pthread_mutex_unlock(&buffer->mutex);
}

/* Worker thread loop. Processes input batches until a completion sentinel is
 * received or the running flag is cleared. A completion batch is forwarded to
 * the sink on exit. */
void *Bp_Worker(void *filter)
{
    Bp_Filter_t *f = (Bp_Filter_t *) filter;

    // Multi-I/O processing mode
    Bp_Batch_t *input_batches[MAX_SOURCES] = {NULL};
    Bp_Batch_t *output_batches[MAX_SINKS] = {NULL};
    Bp_Batch_t input_batch_storage[MAX_SOURCES];
    Bp_Batch_t output_batch_storage[MAX_SINKS];

    // Determine if this filter uses input buffers by checking if buffer[0] is
    // allocated
    bool uses_input_buffers = (f->input_buffers[0].data_ring != NULL);

    // Initialize input batches from our own input buffers
    if (uses_input_buffers) {
        // Read from our own input buffers (at least buffer 0)
        input_batches[0] = &input_batch_storage[0];
        *input_batches[0] = Bp_head(f, &f->input_buffers[0]);

        // If we have additional source connections, read from those too
        for (int i = 1; i < f->n_sources && i < MAX_SOURCES; i++) {
            if (f->input_buffers[i].data_ring != NULL) {
                input_batches[i] = &input_batch_storage[i];
                *input_batches[i] = Bp_head(f, &f->input_buffers[i]);
            }
        }
    }

    // Allocate output batches for sinks
    pthread_mutex_lock(&f->filter_mutex);
    for (int i = 0; i < f->n_sinks && i < MAX_SINKS; i++) {
        if (f->sinks[i]) {
            output_batches[i] = &output_batch_storage[i];
            *output_batches[i] =
                Bp_allocate(f->sinks[i], &f->sinks[i]->input_buffers[0]);

            // If allocation failed due to overflow (drop mode), initialize as
            // empty
            if (output_batches[i]->ec == Bp_EC_NOSPACE) {
                memset(output_batches[i], 0, sizeof(Bp_Batch_t));
                output_batches[i]->ec = Bp_EC_NOSPACE;  // Keep the error status
            }
        }
    }
    pthread_mutex_unlock(&f->filter_mutex);

    // Handle case with no input buffers (source filters like signal generators)
    if (!uses_input_buffers) {
        for (int i = 0; i < MAX_SOURCES; i++) {
            input_batches[i] = &input_batch_storage[i];
            *input_batches[i] =
                (Bp_Batch_t){.data = NULL, .capacity = 0, .ec = Bp_EC_NOINPUT};
        }
    }

    // Check for completion on startup
    bool has_complete = false;
    if (uses_input_buffers) {
        // Check at least the first input buffer
        if (input_batches[0] && input_batches[0]->ec == Bp_EC_COMPLETE) {
            has_complete = true;
        }
        // Check additional source connections if any
        for (int i = 1; i < f->n_sources; i++) {
            if (input_batches[i] && input_batches[i]->ec == Bp_EC_COMPLETE) {
                has_complete = true;
                break;
            }
        }
    }
    if (uses_input_buffers && has_complete) {
        f->running = false;
    }

    while (f->running) {
        // Store initial head positions of output batches
        size_t initial_heads[MAX_SINKS];
        for (int i = 0; i < f->n_sinks; i++) {
            initial_heads[i] = output_batches[i] ? output_batches[i]->head : 0;
        }

        // Pass the correct number of input sources to transform
        int effective_n_inputs =
            uses_input_buffers ? (f->n_sources > 0 ? f->n_sources : 1) : 0;
        f->transform(filter, input_batches, effective_n_inputs, output_batches,
                     f->n_sinks);

        // Distribute output from first batch to remaining outputs if needed
        // This allows transforms to be output-agnostic - they only need to
        // write to output_batches[0] and the framework handles the rest
        if (f->n_sinks > 1 && output_batches[0] &&
            output_batches[0]->head > initial_heads[0]) {
            size_t bytes_written =
                (output_batches[0]->head - initial_heads[0]) * f->data_width;
            void *src = (char *) output_batches[0]->data +
                        initial_heads[0] * f->data_width;

            for (int i = 1; i < f->n_sinks; i++) {
                if (output_batches[i] &&
                    output_batches[i]->ec != Bp_EC_NOSPACE) {
                    // Copy data from first output to this output
                    void *dst = (char *) output_batches[i]->data +
                                output_batches[i]->head * f->data_width;
                    memcpy(dst, src, bytes_written);

                    // Copy metadata from first output
                    output_batches[i]->t_ns = output_batches[0]->t_ns;
                    output_batches[i]->period_ns = output_batches[0]->period_ns;
                    output_batches[i]->dtype = output_batches[0]->dtype;
                    output_batches[i]->meta = output_batches[0]->meta;
                    output_batches[i]->ec = output_batches[0]->ec;

                    // Update head position
                    output_batches[i]->head +=
                        (output_batches[0]->head - initial_heads[0]);
                }
            }
        }

        // Handle input buffer management
        if (uses_input_buffers) {
            // Handle primary input buffer
            if (input_batches[0] &&
                input_batches[0]->head >= input_batches[0]->capacity) {
                Bp_delete_tail(f, &f->input_buffers[0]);
                *input_batches[0] = Bp_head(f, &f->input_buffers[0]);
                if (input_batches[0]->ec == Bp_EC_COMPLETE) {
                    f->running = false;
                    break;
                }
            }

            // Handle additional source connections
            for (int i = 1; i < f->n_sources; i++) {
                if (input_batches[i] &&
                    input_batches[i]->head >= input_batches[i]->capacity) {
                    Bp_delete_tail(f, &f->input_buffers[i]);
                    *input_batches[i] = Bp_head(f, &f->input_buffers[i]);
                    if (input_batches[i]->ec == Bp_EC_COMPLETE) {
                        f->running = false;
                        break;
                    }
                }
            }
        }

        // Handle output buffer management
        pthread_mutex_lock(&f->filter_mutex);
        for (int i = 0; i < f->n_sinks; i++) {
            if (output_batches[i] && f->sinks[i] &&
                output_batches[i]->head >= output_batches[i]->capacity) {
                Bp_submit_batch(f->sinks[i], &f->sinks[i]->input_buffers[0],
                                output_batches[i]);
                *output_batches[i] =
                    Bp_allocate(f->sinks[i], &f->sinks[i]->input_buffers[0]);

                // If allocation failed due to overflow (drop mode), reset to
                // empty batch
                if (output_batches[i]->ec == Bp_EC_NOSPACE) {
                    memset(output_batches[i], 0, sizeof(Bp_Batch_t));
                    output_batches[i]->ec =
                        Bp_EC_NOSPACE;  // Keep the error status
                }
            }
        }
        pthread_mutex_unlock(&f->filter_mutex);
        
        // If we have no inputs and no outputs, yield to prevent CPU spinning
        if (!uses_input_buffers && f->n_sinks == 0) {
            usleep(1000);  // Sleep 1ms
        }
    }

    // Send completion signals to all sinks
    pthread_mutex_lock(&f->filter_mutex);
    for (int i = 0; i < f->n_sinks; i++) {
        if (f->sinks[i]) {
            Bp_Batch_t done = {.ec = Bp_EC_COMPLETE};
            Bp_submit_batch(f->sinks[i], &f->sinks[i]->input_buffers[0], &done);
        }
    }
    pthread_mutex_unlock(&f->filter_mutex);

    return NULL;
}

/* Multi-I/O connection functions */
Bp_EC Bp_add_sink(Bp_Filter_t *filter, Bp_Filter_t *sink)
{
    return Bp_add_sink_with_error(filter, sink, NULL);
}

Bp_EC Bp_add_source(Bp_Filter_t *filter, Bp_Filter_t *source)
{
    if (!filter || !source) return Bp_EC_NOSPACE;
    if (filter->n_sources >= MAX_SOURCES) return Bp_EC_NOSPACE;

    filter->sources[filter->n_sources] = source;
    filter->n_sources++;

    return Bp_EC_OK;
}

Bp_EC Bp_remove_sink(Bp_Filter_t *filter, const Bp_Filter_t *sink)
{
    if (!filter || !sink) return Bp_EC_NOSPACE;

    pthread_mutex_lock(&filter->filter_mutex);
    for (int i = 0; i < filter->n_sinks; i++) {
        if (filter->sinks[i] == sink) {
            // Shift remaining sinks
            for (int j = i; j < filter->n_sinks - 1; j++) {
                filter->sinks[j] = filter->sinks[j + 1];
            }
            filter->sinks[filter->n_sinks - 1] = NULL;
            filter->n_sinks--;
            pthread_mutex_unlock(&filter->filter_mutex);

            return Bp_EC_OK;
        }
    }
    pthread_mutex_unlock(&filter->filter_mutex);
    return Bp_EC_NOINPUT;  // Sink not found
}

Bp_EC Bp_remove_source(Bp_Filter_t *filter, const Bp_Filter_t *source)
{
    if (!filter || !source) return Bp_EC_NOSPACE;

    for (int i = 0; i < filter->n_sources; i++) {
        if (filter->sources[i] == source) {
            // Shift remaining sources
            for (int j = i; j < filter->n_sources - 1; j++) {
                filter->sources[j] = filter->sources[j + 1];
            }
            filter->sources[filter->n_sources - 1] = NULL;
            filter->n_sources--;

            return Bp_EC_OK;
        }
    }
    return Bp_EC_NOINPUT;  // Source not found
}

/* Filter lifecycle functions */
Bp_EC Bp_Filter_Start(Bp_Filter_t *filter)
{
    if (!filter) {
        return Bp_EC_NULL_FILTER;
    }

    if (filter->running) {
        SET_FILTER_ERROR(filter, Bp_EC_ALREADY_RUNNING,
                         "Filter is already running");
        return Bp_EC_ALREADY_RUNNING;
    }

    filter->running = true;

    if (pthread_create(&filter->worker_thread, NULL, &Bp_Worker,
                       (void *) filter) != 0) {
        filter->running = false;
        SET_FILTER_ERROR(filter, Bp_EC_THREAD_CREATE_FAIL,
                         "Failed to create worker thread");
        return Bp_EC_THREAD_CREATE_FAIL;
    }

    return Bp_EC_OK;
}

Bp_EC Bp_Filter_Stop(Bp_Filter_t *filter)
{
    if (!filter) {
        return Bp_EC_NULL_FILTER;
    }

    if (!filter->running) {
        return Bp_EC_OK;  // Already stopped, not an error
    }

    filter->running = false;
    
    // Stop all input buffers to wake up any waiting threads
    for (int i = 0; i < MAX_SOURCES; i++) {
        if (filter->input_buffers[i].data_ring != NULL) {
            BpBatchBuffer_stop(&filter->input_buffers[i]);
        }
    }

    if (pthread_join(filter->worker_thread, NULL) != 0) {
        SET_FILTER_ERROR(filter, Bp_EC_THREAD_JOIN_FAIL,
                         "Failed to join worker thread");
        return Bp_EC_THREAD_JOIN_FAIL;
    }

    return Bp_EC_OK;
}

void BpPassThroughTransform(Bp_Filter_t *filt, Bp_Batch_t **input_batches,
                            int n_inputs, Bp_Batch_t *const *output_batches,
                            int n_outputs)
{
    // Simplified: only write to first output, framework handles distribution
    if (n_inputs > 0 && n_outputs > 0 && input_batches[0] != NULL &&
        output_batches[0] != NULL) {
        Bp_Batch_t *input_batch = input_batches[0];
        Bp_Batch_t *output_batch = output_batches[0];

        size_t available = input_batch->head - input_batch->tail;
        size_t space = output_batch->capacity - output_batch->head;
        size_t ncopy = available < space ? available : space;

        if (ncopy) {
            void *src = (char *) input_batch->data +
                        input_batch->tail * filt->data_width;
            void *dst = (char *) output_batch->data +
                        output_batch->head * filt->data_width;
            memcpy(dst, src, ncopy * filt->data_width);
        }

        // Copy metadata
        output_batch->t_ns = input_batch->t_ns;
        output_batch->period_ns = input_batch->period_ns;
        output_batch->dtype = input_batch->dtype;
        output_batch->meta = input_batch->meta;
        output_batch->ec = input_batch->ec;

        // Update positions
        output_batch->head += ncopy;
        input_batch->tail += ncopy;
    }
}

/* Predefined configurations for common use cases */
const BpFilterConfig BP_CONFIG_FLOAT_STANDARD = {
    .transform = BpPassThroughTransform,
    .dtype = DTYPE_FLOAT,
    .buffer_size = 128,
    .batch_size = 64,
    .number_of_batches_exponent = 6,
    .number_of_input_filters = 1,
    .overflow_behaviour = OVERFLOW_BLOCK,
    .auto_allocate_buffers = true,
    .memory_pool = NULL,
    .alignment = 0,
    .timeout_us = 1000000
};

const BpFilterConfig BP_CONFIG_INT_STANDARD = {
    .transform = BpPassThroughTransform,
    .dtype = DTYPE_INT,
    .buffer_size = 128,
    .batch_size = 64,
    .number_of_batches_exponent = 6,
    .number_of_input_filters = 1,
    .overflow_behaviour = OVERFLOW_BLOCK,
    .auto_allocate_buffers = true,
    .memory_pool = NULL,
    .alignment = 0,
    .timeout_us = 1000000
};

const BpFilterConfig BP_CONFIG_HIGH_THROUGHPUT = {
    .transform = BpPassThroughTransform,
    .dtype = DTYPE_FLOAT,
    .buffer_size = 1024,
    .batch_size = 256,
    .number_of_batches_exponent = 8,
    .number_of_input_filters = 1,
    .overflow_behaviour = OVERFLOW_BLOCK,
    .auto_allocate_buffers = true,
    .memory_pool = NULL,
    .alignment = 0,
    .timeout_us = 1000000
};

const BpFilterConfig BP_CONFIG_LOW_LATENCY = {
    .transform = BpPassThroughTransform,
    .dtype = DTYPE_FLOAT,
    .buffer_size = 32,
    .batch_size = 16,
    .number_of_batches_exponent = 4,
    .number_of_input_filters = 1,
    .overflow_behaviour = OVERFLOW_BLOCK,
    .auto_allocate_buffers = true,
    .memory_pool = NULL,
    .alignment = 0,
    .timeout_us = 1000000
};

/* Internal helper to apply defaults to partial configs */
static void BpFilterConfig_ApplyDefaults(BpFilterConfig* config) {
    if (config->buffer_size == 0) config->buffer_size = 128;
    if (config->batch_size == 0) config->batch_size = 64;
    if (config->number_of_batches_exponent == 0) {
        config->number_of_batches_exponent = 6;
    }
    if (config->number_of_input_filters == 0) {
        config->number_of_input_filters = 1;
    }
}

/* Configuration validation */
Bp_EC BpFilterConfig_Validate(const BpFilterConfig* config) {
    if (!config) {
        return Bp_EC_CONFIG_REQUIRED;
    }
    
    if (!config->transform) {
        return Bp_EC_CONFIG_REQUIRED;
    }
    
    if (config->dtype == DTYPE_NDEF || config->dtype >= DTYPE_MAX) {
        return Bp_EC_INVALID_DTYPE;
    }
    
    if (config->buffer_size == 0) {
        return Bp_EC_INVALID_CONFIG;
    }
    
    if (config->batch_size == 0) {
        return Bp_EC_INVALID_CONFIG;
    }
    
    if (config->number_of_batches_exponent > MAX_CAPACITY_EXPO) {
        return Bp_EC_INVALID_CONFIG;
    }
    
    if (config->number_of_input_filters < 0 || config->number_of_input_filters > MAX_SOURCES) {
        return Bp_EC_INVALID_CONFIG;
    }
    
    return Bp_EC_OK;
}

/* Configuration-based initialization function */
Bp_EC BpFilter_Init(Bp_Filter_t* filter, const BpFilterConfig* config) {
    if (!filter) {
        return Bp_EC_NULL_FILTER;
    }
    
    if (!config) {
        return Bp_EC_CONFIG_REQUIRED;
    }
    
    /* Validate configuration */
    Bp_EC validation_result = BpFilterConfig_Validate(config);
    if (validation_result != Bp_EC_OK) {
        return validation_result;
    }
    
    /* Make a copy of config to apply defaults */
    BpFilterConfig working_config = *config;
    BpFilterConfig_ApplyDefaults(&working_config);
    
    /* Initialize multi-I/O arrays */
    memset(filter->sources, 0, sizeof(filter->sources));
    memset(filter->sinks, 0, sizeof(filter->sinks));
    filter->n_sources = 0;
    filter->n_sinks = 0;
    
    /* Initialize all input buffers to zero first */
    memset(filter->input_buffers, 0, sizeof(filter->input_buffers));
    
    /* Set filter properties from configuration */
    filter->transform = working_config.transform;
    filter->running = false;
    filter->dtype = working_config.dtype;
    filter->data_width = _data_size_lut[working_config.dtype];
    
    /* Initialize timeout to a reasonable value (1 second) */
    filter->timeout.tv_sec = time(NULL) + 1;
    filter->timeout.tv_nsec = 0;
    
    /* Initialize filter mutex */
    if (pthread_mutex_init(&filter->filter_mutex, NULL) != 0) {
        return Bp_EC_MUTEX_INIT_FAIL;
    }
    
    /* Initialize input buffers based on configuration using new enhanced buffer API */
    for (int i = 0; i < working_config.number_of_input_filters && i < MAX_SOURCES; i++) {
        BpBufferConfig_t buffer_config = {
            .batch_size = working_config.batch_size,
            .number_of_batches = 1 << working_config.number_of_batches_exponent,
            .data_width = filter->data_width,
            .dtype = working_config.dtype,
            .overflow_behaviour = working_config.overflow_behaviour,
            .timeout_us = working_config.timeout_us,
            .name = NULL  // Could add debug names later
        };
        
        Bp_EC buffer_init_res = BpBatchBuffer_InitFromConfig(&(filter->input_buffers[i]), &buffer_config);
        if (buffer_init_res != Bp_EC_OK) {
            /* Clean up any previously initialized buffers */
            for (int j = 0; j < i; j++) {
                Bp_BatchBuffer_Deinit(&(filter->input_buffers[j]));
            }
            pthread_mutex_destroy(&filter->filter_mutex);
            return buffer_init_res;
        }
        
        /* Allocate buffers automatically if configured to do so */
        if (working_config.auto_allocate_buffers) {
            Bp_EC alloc_result = Bp_allocate_buffers(filter, i);
            if (alloc_result != Bp_EC_OK) {
                /* Clean up */
                for (int j = 0; j <= i; j++) {
                    Bp_BatchBuffer_Deinit(&(filter->input_buffers[j]));
                }
                pthread_mutex_destroy(&filter->filter_mutex);
                return alloc_result;
            }
        }
    }
    
    return Bp_EC_OK;
}

/* =====================================================
 * Buffer-Centric API Implementation
 * ===================================================== */

/* Initialize buffer from configuration structure */
Bp_EC BpBatchBuffer_InitFromConfig(Bp_BatchBuffer_t* buffer, const BpBufferConfig_t* config)
{
    if (!buffer || !config) return Bp_EC_NOSPACE;

    // Initialize basic buffer state
    buffer->head = 0;
    buffer->tail = 0;
    buffer->ring_capacity_expo = 0;
    buffer->batch_capacity_expo = 0;
    buffer->stopped = false;

    // Initialize statistics
    buffer->total_batches = 0;
    buffer->dropped_batches = 0;
    buffer->blocked_time_ns = 0;

    // Copy configuration into buffer structure
    buffer->data_width = config->data_width;
    buffer->dtype = config->dtype;
    buffer->overflow_behaviour = config->overflow_behaviour;
    buffer->timeout_us = config->timeout_us;

    // Copy debug name if provided
    if (config->name && strlen(config->name) < sizeof(buffer->name)) {
        strncpy(buffer->name, config->name, sizeof(buffer->name) - 1);
        buffer->name[sizeof(buffer->name) - 1] = '\0';
    } else {
        buffer->name[0] = '\0';
    }

    // Calculate ring capacity exponent (find log2 of number_of_batches)
    size_t temp = config->number_of_batches;
    while (temp > 1) {
        buffer->ring_capacity_expo++;
        temp >>= 1;
    }
    if (buffer->ring_capacity_expo > MAX_CAPACITY_EXPO) {
        buffer->ring_capacity_expo = MAX_CAPACITY_EXPO;
    }

    // Calculate batch capacity exponent (find log2 of batch_size)
    temp = config->batch_size;
    while (temp > 1) {
        buffer->batch_capacity_expo++;
        temp >>= 1;
    }
    if (buffer->batch_capacity_expo > MAX_CAPACITY_EXPO) {
        buffer->batch_capacity_expo = MAX_CAPACITY_EXPO;
    }

    // Initialize pthread synchronization objects
    if (pthread_mutex_init(&buffer->mutex, NULL) != 0) {
        return Bp_EC_MUTEX_INIT_FAIL;
    }

    if (pthread_cond_init(&buffer->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&buffer->mutex);
        return Bp_EC_COND_INIT_FAIL;
    }

    if (pthread_cond_init(&buffer->not_full, NULL) != 0) {
        pthread_mutex_destroy(&buffer->mutex);
        pthread_cond_destroy(&buffer->not_empty);
        return Bp_EC_COND_INIT_FAIL;
    }

    // Allocate ring buffers
    buffer->data_ring = malloc(buffer->data_width * Bp_ring_capacity(buffer));
    buffer->batch_ring = malloc(sizeof(Bp_Batch_t) * Bp_ring_capacity(buffer));

    if (!buffer->data_ring || !buffer->batch_ring) {
        free(buffer->data_ring);
        free(buffer->batch_ring);
        pthread_mutex_destroy(&buffer->mutex);
        pthread_cond_destroy(&buffer->not_empty);
        pthread_cond_destroy(&buffer->not_full);
        return Bp_EC_NOSPACE;
    }

    return Bp_EC_OK;
}

/* Create and initialize buffer in one step */
Bp_BatchBuffer_t* BpBatchBuffer_Create(const BpBufferConfig_t* config)
{
    if (!config) return NULL;

    Bp_BatchBuffer_t* buffer = malloc(sizeof(Bp_BatchBuffer_t));
    if (!buffer) return NULL;

    Bp_EC result = BpBatchBuffer_InitFromConfig(buffer, config);
    if (result != Bp_EC_OK) {
        free(buffer);
        return NULL;
    }

    return buffer;
}

/* Destroy buffer created with BpBatchBuffer_Create */
void BpBatchBuffer_Destroy(Bp_BatchBuffer_t* buffer)
{
    if (!buffer) return;
    
    Bp_BatchBuffer_Deinit(buffer);
    free(buffer);
}

/* Buffer-centric core operations */
Bp_Batch_t BpBatchBuffer_Allocate(Bp_BatchBuffer_t* buf)
{
    return BpBatchBuffer_Allocate_Inline(buf);
}

Bp_EC BpBatchBuffer_Submit(Bp_BatchBuffer_t* buf, const Bp_Batch_t* batch)
{
    return BpBatchBuffer_Submit_Inline(buf, batch);
}

Bp_Batch_t BpBatchBuffer_Head(Bp_BatchBuffer_t* buf)
{
    return BpBatchBuffer_Head_Inline(buf);
}

Bp_EC BpBatchBuffer_DeleteTail(Bp_BatchBuffer_t* buf)
{
    return BpBatchBuffer_DeleteTail_Inline(buf);
}

/* Utility operations */
bool BpBatchBuffer_IsEmpty(const Bp_BatchBuffer_t* buf)
{
    return BpBatchBuffer_IsEmpty_Inline(buf);
}

bool BpBatchBuffer_IsFull(const Bp_BatchBuffer_t* buf)
{
    return BpBatchBuffer_IsFull_Inline(buf);
}

size_t BpBatchBuffer_Available(const Bp_BatchBuffer_t* buf)
{
    return BpBatchBuffer_Available_Inline(buf);
}

size_t BpBatchBuffer_Capacity(const Bp_BatchBuffer_t* buf)
{
    return BpBatchBuffer_Capacity_Inline(buf);
}

/* Control operations */
void BpBatchBuffer_Stop(Bp_BatchBuffer_t* buf)
{
    BpBatchBuffer_stop(buf);  // Use existing implementation
}

void BpBatchBuffer_Reset(Bp_BatchBuffer_t* buf)
{
    if (!buf) return;
    
    pthread_mutex_lock(&buf->mutex);
    buf->head = 0;
    buf->tail = 0;
    buf->stopped = false;
    buf->total_batches = 0;
    buf->dropped_batches = 0;
    buf->blocked_time_ns = 0;
    pthread_mutex_unlock(&buf->mutex);
}

/* Configuration updates (thread-safe) */
Bp_EC BpBatchBuffer_SetTimeout(Bp_BatchBuffer_t* buf, unsigned long timeout_us)
{
    if (!buf) return Bp_EC_NOSPACE;
    
    pthread_mutex_lock(&buf->mutex);
    buf->timeout_us = timeout_us;
    pthread_mutex_unlock(&buf->mutex);
    
    return Bp_EC_OK;
}

Bp_EC BpBatchBuffer_SetOverflowBehaviour(Bp_BatchBuffer_t* buf, OverflowBehaviour_t behaviour)
{
    if (!buf) return Bp_EC_NOSPACE;
    
    pthread_mutex_lock(&buf->mutex);
    buf->overflow_behaviour = behaviour;
    pthread_mutex_unlock(&buf->mutex);
    
    return Bp_EC_OK;
}

/* Enhanced connection function with detailed error reporting */
Bp_EC Bp_add_sink_with_error(Bp_Filter_t* source, Bp_Filter_t* sink, BpTypeError* error) {
    if (!source || !sink) {
        if (error) {
            error->code = Bp_EC_NULL_FILTER;
            error->message = "Source or sink filter is NULL";
            error->expected_type = DTYPE_NDEF;
            error->actual_type = DTYPE_NDEF;
        }
        return Bp_EC_NULL_FILTER;
    }
    
    /* Type checking */
    if (source->dtype != DTYPE_NDEF && sink->dtype != DTYPE_NDEF && 
        source->dtype != sink->dtype) {
        if (error) {
            error->code = Bp_EC_DTYPE_MISMATCH;
            error->message = "Source and sink data types do not match";
            error->expected_type = sink->dtype;
            error->actual_type = source->dtype;
        }
        return Bp_EC_DTYPE_MISMATCH;
    }
    
    /* Data width checking */
    if (source->data_width != 0 && sink->data_width != 0 &&
        source->data_width != sink->data_width) {
        if (error) {
            error->code = Bp_EC_WIDTH_MISMATCH;
            error->message = "Source and sink data widths do not match";
            error->expected_type = sink->dtype;
            error->actual_type = source->dtype;
        }
        return Bp_EC_WIDTH_MISMATCH;
    }
    
    /* If type checking passed, perform the actual connection */
    pthread_mutex_lock(&source->filter_mutex);
    if (source->n_sinks >= MAX_SINKS) {
        pthread_mutex_unlock(&source->filter_mutex);
        if (error) {
            error->code = Bp_EC_NOSPACE;
            error->message = "Maximum number of sinks reached";
            error->expected_type = sink->dtype;
            error->actual_type = source->dtype;
        }
        return Bp_EC_NOSPACE;
    }

    source->sinks[source->n_sinks] = sink;
    source->n_sinks++;
    pthread_mutex_unlock(&source->filter_mutex);
    
    if (error) {
        error->code = Bp_EC_OK;
        error->message = "Connection successful";
        error->expected_type = sink->dtype;
        error->actual_type = source->dtype;
    }
    
    return Bp_EC_OK;
}
