#include "core.h"
#include <stdio.h>
#include <string.h>

 Bp_EC Bp_Filter_Init(Bp_Filter_t *filter, TransformFcn_t transform_function, int initial_state, 
                     size_t buffer_size_expo, int batch_size_expo, int number_of_batches_exponent, int number_of_input_filters) {
     if (buffer_size_expo <= 0) buffer_size_expo = 10;
     if (number_of_batches_exponent <= 0) number_of_batches_exponent = 10;
     
    filter->buffer.ring_capacity_expo= buffer_size_expo;
    filter->buffer.batch_capacity_expo= number_of_batches_exponent;
    // Assuming other initializations are successful for this example.
    return Bp_EC_OK;
}

Bp_EC BpFilter_Init(Bp_Filter_t *filter, TransformFcn_t transform_function, int initial_state, 
                    size_t buffer_size, int batch_size, int number_of_batches_exponent, int number_of_input_filters) {
    if (batch_size <= 0) batch_size = 64;
    if (number_of_batches_exponent <= 0) number_of_batches_exponent = 64;

    Bp_EC buffer_init_res = Bp_BatchBuffer_Init(&(filter->buffer), batch_size, 1 << number_of_batches_exponent);
    if (buffer_init_res != Bp_EC_OK) {
        return buffer_init_res;
    }

    filter->transform = transform_function;
    filter->n_input_buffers = number_of_input_filters;
    // Additional initialization may be required depending on structure specifics
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
        Bp_Batch_t input_batch = f->has_input_buffer ?
                Bp_head(f, &f->buffer) :
                (Bp_Batch_t){ .data = NULL, .capacity = 0, .ec = Bp_EC_NOINPUT };
        Bp_Batch_t output_batch = f->sink ?
                Bp_allocate(f->sink, &f->sink->buffer) :
                (Bp_Batch_t){ .data = malloc(1024 * f->data_width), .capacity = 1024 };

        if (f->has_input_buffer && input_batch.ec == Bp_EC_COMPLETE)
                f->running = false;

        while (f->running) {
                f->transform(filter, &input_batch, &output_batch);

                if (f->has_input_buffer &&
                    (input_batch.head >= input_batch.capacity)) {
                        Bp_delete_tail(f, &f->buffer);
                        input_batch = Bp_head(f, &f->buffer);
                        if (input_batch.ec == Bp_EC_COMPLETE) {
                                f->running = false;
                                break;
                        }
                }
                assert(output_batch.head <= output_batch.capacity);
                assert(output_batch.tail <= output_batch.capacity);
                assert(output_batch.tail <= output_batch.head);

                if (output_batch.head >= output_batch.capacity) {
                        if (f->sink) {
                                Bp_submit_batch(f->sink, &f->sink->buffer,
                                                &output_batch);
                                output_batch = Bp_allocate(f->sink,
                                                          &f->sink->buffer);
                        } else {
                                output_batch.head = 0;
                                output_batch.tail = 0;
                        }
                }
        }

        if (f->sink) {
                Bp_Batch_t done = { .ec = Bp_EC_COMPLETE };
                Bp_submit_batch(f->sink, &f->sink->buffer, &done);
        }
        return NULL;
}



void BpPassThroughTransform(Bp_Filter_t* filt, Bp_Batch_t *input_batch, Bp_Batch_t *output_batch){
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

        input_batch->tail  += ncopy;
        output_batch->head += ncopy;
}

