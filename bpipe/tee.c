#include "tee.h"
#include <string.h>
#include <stdint.h>

Bp_EC BpTeeFilter_Init(Bp_TeeFilter_t* tee, size_t buffer_size, int batch_size,
                       int number_of_batches_exponent)
{
    if (!tee) return Bp_EC_NOSPACE;

    // Initialize base filter with BpTeeTransform
    // Tee filters have 1 input
    return BpFilter_Init(&tee->base, BpTeeTransform, 0, buffer_size, batch_size,
                         number_of_batches_exponent, 1);
}

void BpTeeTransform(Bp_Filter_t* filt, Bp_Batch_t** input_batches, int n_inputs,
                    Bp_Batch_t* const* output_batches, int n_outputs)
{
    // Tee filter explicitly copies to ALL outputs
    // This is for cases where you want explicit control over fan-out
    if (n_inputs > 0 && input_batches[0] != NULL) {
        Bp_Batch_t* input_batch = input_batches[0];
        size_t min_copied = SIZE_MAX;  // Track minimum amount copied

        for (int i = 0; i < n_outputs; i++) {
            if (output_batches[i] != NULL) {
                Bp_Batch_t* output_batch = output_batches[i];

                size_t available = input_batch->head - input_batch->tail;
                size_t space = output_batch->capacity - output_batch->head;
                size_t ncopy = available < space ? available : space;

                if (ncopy) {
                    void* src = (char*) input_batch->data +
                                input_batch->tail * filt->data_width;
                    void* dst = (char*) output_batch->data +
                                output_batch->head * filt->data_width;
                    memcpy(dst, src, ncopy * filt->data_width);
                }

                // Copy metadata
                output_batch->t_ns = input_batch->t_ns;
                output_batch->period_ns = input_batch->period_ns;
                output_batch->dtype = input_batch->dtype;
                output_batch->meta = input_batch->meta;
                output_batch->ec = input_batch->ec;

                output_batch->head += ncopy;
                
                // Track the minimum amount copied
                if (ncopy < min_copied) {
                    min_copied = ncopy;
                }
            }
        }

        // Update input batch tail based on smallest copy
        if (min_copied != SIZE_MAX && min_copied > 0) {
            input_batch->tail += min_copied;
        }
    }
}