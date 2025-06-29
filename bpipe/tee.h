#ifndef BP_TEE_H
#define BP_TEE_H

#include "core.h"

/* Tee filter - explicitly distributes input to multiple outputs
 *
 * This is a special-purpose filter for when you need explicit
 * control over fan-out behavior, rather than using the automatic
 * distribution provided by the framework.
 *
 * Use cases:
 * - When you need different processing logic for different outputs
 * - When automatic distribution doesn't meet your requirements
 * - When you want to make fan-out explicit in your pipeline
 *
 * Note: For simple 1:N distribution, the framework's automatic
 * distribution (via Bp_add_sink) is usually sufficient.
 */
typedef struct {
    Bp_Filter_t base;
} Bp_TeeFilter_t;

/* Initialize a tee filter */
Bp_EC BpTeeFilter_Init(Bp_TeeFilter_t* tee, SampleDtype_t dtype, size_t buffer_size, 
                       int batch_size, int number_of_batches_exponent);

/* Tee transform function - copies input to ALL outputs explicitly */
void BpTeeTransform(Bp_Filter_t* filt, Bp_Batch_t** input_batches, int n_inputs,
                    Bp_Batch_t* const* output_batches, int n_outputs);

#endif /* BP_TEE_H */