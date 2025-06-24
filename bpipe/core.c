Bp_EC Bp_BatchBuffer_Init(Bp_BatchBuffer_t *buffer, size_t batch_size, size_t number_of_batches) {
    if (batch_size <= 0) batch_size = 64;
    if (number_of_batches <= 0) number_of_batches = 64;

    buffer->batch_size = batch_size;
    buffer->number_of_batches = number_of_batches;
    // Assuming other initializations are successful for this example.
    return BP_SUCCESS;
}

Bp_EC BpFilter_Init(Bp_Filter_t *filter, TransformFcn_t transform_function, int initial_state, 
                    size_t buffer_size, int batch_size, int number_of_batches_exponent, int number_of_input_filters) {
    if (batch_size <= 0) batch_size = 64;
    if (number_of_batches_exponent <= 0) number_of_batches_exponent = 64;

    int res;
    res = pthread_mutex_init(&(filter->mutex), NULL);
    if (res != 0) return BP_ERROR_MUTEX_INIT_FAIL;

    res = pthread_cond_init(&(filter->not_full), NULL);
    if (res != 0) {
        pthread_mutex_destroy(&(filter->mutex));
        return BP_ERROR_COND_INIT_FAIL;
    }
    res = pthread_cond_init(&(filter->not_empty), NULL);
    if (res != 0) {
        pthread_mutex_destroy(&(filter->mutex));
        pthread_cond_destroy(&(filter->not_full));
        return BP_ERROR_COND_INIT_FAIL;
    }
    
    Bp_EC buffer_init_res = Bp_BatchBuffer_Init(&(filter->batch_buffer), batch_size, 1 << number_of_batches_exponent);
    if (buffer_init_res != BP_SUCCESS) {
        pthread_mutex_destroy(&(filter->mutex));
        pthread_cond_destroy(&(filter->not_full));
        pthread_cond_destroy(&(filter->not_empty));
        return BP_ERROR_BUFFER_INIT_FAIL;
    }

    filter->buffer_size = buffer_size;
    filter->state = initial_state;
    filter->transform = transform_function;
    filter->number_of_input_filters = number_of_input_filters;
    // Additional initialization may be required depending on structure specifics
    return BP_SUCCESS;
}
#include "core.h"
#include <stdio.h>

const size_t _data_size_lut[] = {
    [DTYPE_FLOAT] = sizeof(float),
    [DTYPE_INT] = sizeof(int),
    [DTYPE_UNSIGNED] = sizeof(unsigned),
};

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

