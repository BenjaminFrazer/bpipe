#include "core.h"
#include <stdio.h>
#include <string.h>

 Bp_EC Bp_Filter_Init(Bp_Filter_t *filter, TransformFcn_t transform_function, int initial_state, 
                     size_t buffer_size_expo, int batch_size_expo, int number_of_batches_exponent, int number_of_input_filters) {
     if (buffer_size_expo <= 0) buffer_size_expo = 10;
     if (number_of_batches_exponent <= 0) number_of_batches_exponent = 10;
     
    // Initialize first input buffer for compatibility
    filter->input_buffers[0].ring_capacity_expo = buffer_size_expo;
    filter->input_buffers[0].batch_capacity_expo = number_of_batches_exponent;
    
    filter->transform = transform_function;
    filter->running = false;
    filter->n_sources = 0;
    filter->n_sinks = 0;
    memset(filter->sources, 0, sizeof(filter->sources));
    memset(filter->sinks, 0, sizeof(filter->sinks));
    
    return Bp_EC_OK;
}

Bp_EC BpFilter_Init(Bp_Filter_t *filter, TransformFcn_t transform_function, int initial_state, 
                    size_t buffer_size, int batch_size, int number_of_batches_exponent, int number_of_input_filters) {
    if (batch_size <= 0) batch_size = 64;
    if (number_of_batches_exponent <= 0) number_of_batches_exponent = 64;

    // Initialize multi-I/O arrays
    memset(filter->sources, 0, sizeof(filter->sources));
    memset(filter->sinks, 0, sizeof(filter->sinks));
    filter->n_sources = 0;
    filter->n_sinks = 0;
    
    // Initialize input buffers based on number_of_input_filters
    for (int i = 0; i < number_of_input_filters && i < MAX_SOURCES; i++) {
        Bp_EC buffer_init_res = Bp_BatchBuffer_Init(&(filter->input_buffers[i]), batch_size, 1 << number_of_batches_exponent);
        if (buffer_init_res != Bp_EC_OK) {
            return buffer_init_res;
        }
    }

    filter->transform = transform_function;
    filter->running = false;
    return Bp_EC_OK;
}

const size_t _data_size_lut[] = {
    [DTYPE_FLOAT] = sizeof(float),
    [DTYPE_INT] = sizeof(int),
    [DTYPE_UNSIGNED] = sizeof(unsigned),
};

Bp_EC Bp_BatchBuffer_Init(Bp_BatchBuffer_t *buffer, size_t batch_size, size_t number_of_batches) {
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
        return BP_ERROR_MUTEX_INIT_FAIL;
    }
    
    if (pthread_cond_init(&buffer->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&buffer->mutex);
        return BP_ERROR_COND_INIT_FAIL;
    }
    
    if (pthread_cond_init(&buffer->not_full, NULL) != 0) {
        pthread_mutex_destroy(&buffer->mutex);
        pthread_cond_destroy(&buffer->not_empty);
        return BP_ERROR_COND_INIT_FAIL;
    }
    
    // Allocate ring buffers (will be done later by Bp_allocate_buffers)
    buffer->data_ring = NULL;
    buffer->batch_ring = NULL;
    
    return Bp_EC_OK;
}

Bp_EC Bp_BatchBuffer_Deinit(Bp_BatchBuffer_t *buffer) {
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

/* Worker thread loop. Processes input batches until a completion sentinel is
 * received or the running flag is cleared. A completion batch is forwarded to
 * the sink on exit. */
void* Bp_Worker(void* filter) {
        Bp_Filter_t* f = (Bp_Filter_t*)filter;
        
        // Multi-I/O processing mode
        Bp_Batch_t* input_batches[MAX_SOURCES] = {NULL};
        Bp_Batch_t* output_batches[MAX_SINKS] = {NULL};
        Bp_Batch_t input_batch_storage[MAX_SOURCES];
        Bp_Batch_t output_batch_storage[MAX_SINKS];
        
        // Determine if this filter uses input buffers by checking if buffer[0] is allocated
        bool uses_input_buffers = (f->input_buffers[0].data_ring != NULL);
        
        // Initialize input batches from our own input buffers
        if (uses_input_buffers) {
            // Read from our own input buffers (at least buffer 0)
            input_batches[0] = &input_batch_storage[0];
            *input_batches[0] = Bp_head(f, &f->input_buffers[0]);
            
            // If we have additional source connections, read from those too
            for (int i = 1; i < f->n_sources && i < MAX_SOURCES; i++) {
                input_batches[i] = &input_batch_storage[i];
                *input_batches[i] = Bp_head(f, &f->input_buffers[i]);
            }
        }
        
        // Allocate output batches for sinks
        for (int i = 0; i < f->n_sinks && i < MAX_SINKS; i++) {
            if (f->sinks[i]) {
                output_batches[i] = &output_batch_storage[i];
                *output_batches[i] = Bp_allocate(f->sinks[i], &f->sinks[i]->input_buffers[0]);
            }
        }
        
        // Handle case with no input buffers (source filters like signal generators)
        if (!uses_input_buffers) {
            for (int i = 0; i < MAX_SOURCES; i++) {
                input_batches[i] = &input_batch_storage[i];
                *input_batches[i] = (Bp_Batch_t){ .data = NULL, .capacity = 0, .ec = Bp_EC_NOINPUT };
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
            // Pass the correct number of input sources to transform
            int effective_n_inputs = uses_input_buffers ? (f->n_sources > 0 ? f->n_sources : 1) : 0;
            f->transform(filter, input_batches, effective_n_inputs, output_batches, f->n_sinks);
            
            // Handle input buffer management
            if (uses_input_buffers) {
                // Handle primary input buffer
                if (input_batches[0] && input_batches[0]->head >= input_batches[0]->capacity) {
                    Bp_delete_tail(f, &f->input_buffers[0]);
                    *input_batches[0] = Bp_head(f, &f->input_buffers[0]);
                    if (input_batches[0]->ec == Bp_EC_COMPLETE) {
                        f->running = false;
                        break;
                    }
                }
                
                // Handle additional source connections
                for (int i = 1; i < f->n_sources; i++) {
                    if (input_batches[i] && input_batches[i]->head >= input_batches[i]->capacity) {
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
            for (int i = 0; i < f->n_sinks; i++) {
                if (output_batches[i] && f->sinks[i] && 
                    output_batches[i]->head >= output_batches[i]->capacity) {
                    Bp_submit_batch(f->sinks[i], &f->sinks[i]->input_buffers[0], output_batches[i]);
                    *output_batches[i] = Bp_allocate(f->sinks[i], &f->sinks[i]->input_buffers[0]);
                }
            }
        }
        
        // Send completion signals to all sinks
        for (int i = 0; i < f->n_sinks; i++) {
            if (f->sinks[i]) {
                Bp_Batch_t done = { .ec = Bp_EC_COMPLETE };
                Bp_submit_batch(f->sinks[i], &f->sinks[i]->input_buffers[0], &done);
            }
        }
        
        return NULL;
}



/* Multi-I/O connection functions */
Bp_EC Bp_add_sink(Bp_Filter_t *filter, Bp_Filter_t *sink) {
    if (!filter || !sink) return Bp_EC_NOSPACE;
    if (filter->n_sinks >= MAX_SINKS) return Bp_EC_NOSPACE;
    
    filter->sinks[filter->n_sinks] = sink;
    filter->n_sinks++;
    
    return Bp_EC_OK;
}

Bp_EC Bp_add_source(Bp_Filter_t *filter, Bp_Filter_t *source) {
    if (!filter || !source) return Bp_EC_NOSPACE;
    if (filter->n_sources >= MAX_SOURCES) return Bp_EC_NOSPACE;
    
    filter->sources[filter->n_sources] = source;
    filter->n_sources++;
    
    return Bp_EC_OK;
}

Bp_EC Bp_remove_sink(Bp_Filter_t *filter, Bp_Filter_t *sink) {
    if (!filter || !sink) return Bp_EC_NOSPACE;
    
    for (int i = 0; i < filter->n_sinks; i++) {
        if (filter->sinks[i] == sink) {
            // Shift remaining sinks
            for (int j = i; j < filter->n_sinks - 1; j++) {
                filter->sinks[j] = filter->sinks[j + 1];
            }
            filter->sinks[filter->n_sinks - 1] = NULL;
            filter->n_sinks--;
            
            return Bp_EC_OK;
        }
    }
    return Bp_EC_NOINPUT; // Sink not found
}

Bp_EC Bp_remove_source(Bp_Filter_t *filter, Bp_Filter_t *source) {
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
    return Bp_EC_NOINPUT; // Source not found
}

/* Filter lifecycle functions */
Bp_EC Bp_Filter_Start(Bp_Filter_t *filter) {
    if (!filter) {
        return BP_ERROR_NULL_FILTER;
    }
    
    if (filter->running) {
        SET_FILTER_ERROR(filter, BP_ERROR_ALREADY_RUNNING, "Filter is already running");
        return BP_ERROR_ALREADY_RUNNING;
    }
    
    filter->running = true;
    
    if (pthread_create(&filter->worker_thread, NULL, &Bp_Worker, (void*)filter) != 0) {
        filter->running = false;
        SET_FILTER_ERROR(filter, BP_ERROR_THREAD_CREATE_FAIL, "Failed to create worker thread");
        return BP_ERROR_THREAD_CREATE_FAIL;
    }
    
    return Bp_EC_OK;
}

Bp_EC Bp_Filter_Stop(Bp_Filter_t *filter) {
    if (!filter) {
        return BP_ERROR_NULL_FILTER;
    }
    
    if (!filter->running) {
        return Bp_EC_OK; // Already stopped, not an error
    }
    
    filter->running = false;
    
    if (pthread_join(filter->worker_thread, NULL) != 0) {
        SET_FILTER_ERROR(filter, BP_ERROR_THREAD_JOIN_FAIL, "Failed to join worker thread");
        return BP_ERROR_THREAD_JOIN_FAIL;
    }
    
    return Bp_EC_OK;
}

void BpPassThroughTransform(Bp_Filter_t* filt, Bp_Batch_t **input_batches, int n_inputs, Bp_Batch_t **output_batches, int n_outputs) {
    // Multi-I/O transform: copy first input to all outputs
    if (n_inputs > 0 && n_outputs > 0 && input_batches[0] != NULL) {
        Bp_Batch_t *input_batch = input_batches[0];
        
        for (int i = 0; i < n_outputs; i++) {
            if (output_batches[i] != NULL) {
                Bp_Batch_t *output_batch = output_batches[i];
                
                size_t available = input_batch->head - input_batch->tail;
                size_t space     = output_batch->capacity - output_batch->head;
                size_t ncopy     = available < space ? available : space;

                if(ncopy){
                        void* src = (char*)input_batch->data + input_batch->tail * filt->data_width;
                        void* dst = (char*)output_batch->data + output_batch->head * filt->data_width;
                        memcpy(dst, src, ncopy * filt->data_width);
                }

                output_batch->t_ns      = input_batch->t_ns;
                output_batch->period_ns = input_batch->period_ns;
                output_batch->dtype     = input_batch->dtype;
                output_batch->meta      = input_batch->meta;
                output_batch->ec        = input_batch->ec;

                output_batch->head += ncopy;
            }
        }
        // Update input batch tail once for all outputs
        if (n_outputs > 0 && output_batches[0] != NULL) {
            size_t available = input_batch->head - input_batch->tail;
            size_t space = output_batches[0]->capacity - output_batches[0]->head;
            size_t ncopy = available < space ? available : space;
            input_batch->tail += ncopy;
        }
    }
}

