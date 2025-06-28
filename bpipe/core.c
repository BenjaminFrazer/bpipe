#define _GNU_SOURCE
#include "core.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

Bp_EC Bp_Filter_Init(Bp_Filter_t *filter, TransformFcn_t transform_function,
                     int initial_state, size_t buffer_size_expo,
                     int batch_size_expo, int number_of_batches_exponent,
                     int number_of_input_filters)
{
    if (buffer_size_expo <= 0) buffer_size_expo = 10;
    if (number_of_batches_exponent <= 0) number_of_batches_exponent = 10;

    // Initialize first input buffer for compatibility
    filter->input_buffers[0].ring_capacity_expo = buffer_size_expo;
    filter->input_buffers[0].batch_capacity_expo = number_of_batches_exponent;

    filter->transform = transform_function;
    filter->running = false;
    filter->n_sources = 0;
    filter->n_sinks = 0;
    filter->overflow_behaviour =
        OVERFLOW_BLOCK;  // Default to blocking behavior
    memset(filter->sources, 0, sizeof(filter->sources));
    memset(filter->sinks, 0, sizeof(filter->sinks));
    memset(filter->input_buffers, 0, sizeof(filter->input_buffers));

    // Initialize timeout to a reasonable value (1 second)
    filter->timeout.tv_sec = time(NULL) + 1;
    filter->timeout.tv_nsec = 0;

    // Initialize filter mutex
    if (pthread_mutex_init(&filter->filter_mutex, NULL) != 0) {
        return Bp_EC_MUTEX_INIT_FAIL;
    }

    return Bp_EC_OK;
}

Bp_EC BpFilter_Init(Bp_Filter_t *filter, TransformFcn_t transform_function,
                    int initial_state, size_t buffer_size, int batch_size,
                    int number_of_batches_exponent, int number_of_input_filters)
{
    if (batch_size <= 0) batch_size = 64;
    if (number_of_batches_exponent <= 0) number_of_batches_exponent = 64;

    // Initialize multi-I/O arrays
    memset(filter->sources, 0, sizeof(filter->sources));
    memset(filter->sinks, 0, sizeof(filter->sinks));
    filter->n_sources = 0;
    filter->n_sinks = 0;
    
    // Initialize all input buffers to zero first
    memset(filter->input_buffers, 0, sizeof(filter->input_buffers));

    // Initialize input buffers based on number_of_input_filters
    for (int i = 0; i < number_of_input_filters && i < MAX_SOURCES; i++) {
        Bp_EC buffer_init_res =
            Bp_BatchBuffer_Init(&(filter->input_buffers[i]), batch_size,
                                1 << number_of_batches_exponent);
        if (buffer_init_res != Bp_EC_OK) {
            return buffer_init_res;
        }
    }

    filter->transform = transform_function;
    filter->running = false;
    filter->overflow_behaviour =
        OVERFLOW_BLOCK;  // Default to blocking behavior

    // Initialize timeout to a reasonable value (1 second)
    filter->timeout.tv_sec = time(NULL) + 1;
    filter->timeout.tv_nsec = 0;

    // Initialize filter mutex
    if (pthread_mutex_init(&filter->filter_mutex, NULL) != 0) {
        return Bp_EC_MUTEX_INIT_FAIL;
    }
    return Bp_EC_OK;
}

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
    if (!filter || !sink) return Bp_EC_NOSPACE;

    pthread_mutex_lock(&filter->filter_mutex);
    if (filter->n_sinks >= MAX_SINKS) {
        pthread_mutex_unlock(&filter->filter_mutex);
        return Bp_EC_NOSPACE;
    }

    filter->sinks[filter->n_sinks] = sink;
    filter->n_sinks++;
    pthread_mutex_unlock(&filter->filter_mutex);

    return Bp_EC_OK;
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
