#include "math_ops.h"
#include <math.h>

/* Internal helper for common initialization */
static Bp_EC BpMathOp_InitCommon(Bp_Filter_t* filter,
                                 const BpMathOpConfig* math_config,
                                 TransformFcn_t* transform)
{
    if (!filter || !math_config) {
        return Bp_EC_NULL_FILTER;
    }

    // Copy config and set transform
    BpFilterConfig config = math_config->base_config;
    config.transform = transform;

    // Validate dtype
    if (config.dtype == DTYPE_NDEF) {
        return Bp_EC_INVALID_DTYPE;
    }

    // Initialize base filter
    Bp_EC ec = BpFilter_Init(filter, &config);
    if (ec != Bp_EC_OK) {
        return ec;
    }

    // Store math-specific properties if needed
    // (for now, in_place and check_overflow are handled in transform)

    return Bp_EC_OK;
}

/* BpMultiplyConst Implementation */

Bp_EC BpMultiplyConst_Init(BpMultiplyConst_t* op,
                           const BpMultiplyConstConfig* config)
{
    if (!op || !config) {
        return Bp_EC_NULL_FILTER;
    }

    // Store operation-specific parameter
    op->scale = config->value;

    // Initialize base filter
    return BpMathOp_InitCommon(&op->base, &config->math_config,
                               BpMultiplyConstTransform);
}

void BpMultiplyConstTransform(Bp_Filter_t* filter, Bp_Batch_t** inputs,
                              int n_inputs, Bp_Batch_t* const* outputs,
                              int n_outputs)
{
    BpMultiplyConst_t* op = (BpMultiplyConst_t*) filter;
    Bp_Batch_t* in = inputs[0];
    Bp_Batch_t* out = outputs[0];

    // Copy batch metadata
    out->head = in->head;
    out->tail = in->tail;
    out->capacity = in->capacity;
    out->t_ns = in->t_ns;
    out->period_ns = in->period_ns;
    out->batch_id = in->batch_id;
    out->ec = in->ec;
    out->meta = in->meta;
    out->dtype = in->dtype;

    // Check for pass-through conditions
    if (in->ec != Bp_EC_OK || in->tail <= in->head) {
        return;
    }

    size_t n_samples = in->tail - in->head;

    // Apply operation based on dtype
    switch (in->dtype) {
        case DTYPE_FLOAT: {
            float* in_data = (float*) in->data + in->head;
            float* out_data = (float*) out->data + out->head;
            for (size_t i = 0; i < n_samples; i++) {
                out_data[i] = in_data[i] * op->scale;
            }
            break;
        }
        case DTYPE_INT: {
            int* in_data = (int*) in->data + in->head;
            int* out_data = (int*) out->data + out->head;
            for (size_t i = 0; i < n_samples; i++) {
                out_data[i] = (int) (in_data[i] * op->scale);
            }
            break;
        }
        case DTYPE_UNSIGNED: {
            unsigned* in_data = (unsigned*) in->data + in->head;
            unsigned* out_data = (unsigned*) out->data + out->head;
            for (size_t i = 0; i < n_samples; i++) {
                out_data[i] = (unsigned) (in_data[i] * op->scale);
            }
            break;
        }
        default:
            SET_FILTER_ERROR(filter, Bp_EC_INVALID_DTYPE, "Unsupported dtype");
            break;
    }
}

/* BpMultiplyMulti Implementation */

Bp_EC BpMultiplyMulti_Init(BpMultiplyMulti_t* op,
                           const BpMultiplyMultiConfig* config)
{
    if (!op || !config) {
        return Bp_EC_NULL_FILTER;
    }

    // Validate n_inputs from base config
    int n_inputs = config->math_config.base_config.number_of_input_filters;
    if (n_inputs < 2 || n_inputs > MAX_SOURCES) {
        return Bp_EC_INVALID_CONFIG;
    }

    // Initialize base filter
    return BpMathOp_InitCommon(&op->base, &config->math_config,
                               BpMultiplyMultiTransform);
}

void BpMultiplyMultiTransform(Bp_Filter_t* filter, Bp_Batch_t** inputs,
                              int n_inputs, Bp_Batch_t* const* outputs,
                              int n_outputs)
{
    Bp_Batch_t* out = outputs[0];

    // Validate we have the expected number of inputs
    if (n_inputs < 2) {
        SET_FILTER_ERROR(filter, Bp_EC_NOINPUT,
                         "MultiplyMulti requires at least 2 inputs");
        return;
    }

    // Use first input as reference for metadata and size
    Bp_Batch_t* first = inputs[0];

    // Copy metadata from first input
    out->head = first->head;
    out->tail = first->tail;
    out->capacity = first->capacity;
    out->t_ns = first->t_ns;
    out->period_ns = first->period_ns;
    out->batch_id = first->batch_id;
    out->ec = first->ec;
    out->meta = first->meta;
    out->dtype = first->dtype;

    // Check for pass-through conditions
    if (first->ec != Bp_EC_OK || first->tail <= first->head) {
        return;
    }

    size_t n_samples = first->tail - first->head;

    // Validate all inputs have same size and dtype
    for (int i = 1; i < n_inputs; i++) {
        if (inputs[i]->dtype != first->dtype) {
            SET_FILTER_ERROR(filter, Bp_EC_DTYPE_MISMATCH,
                             "Input dtypes don't match");
            return;
        }
        if ((inputs[i]->tail - inputs[i]->head) != n_samples) {
            SET_FILTER_ERROR(filter, Bp_EC_WIDTH_MISMATCH,
                             "Input sizes don't match");
            return;
        }
    }

    // Apply operation based on dtype
    switch (first->dtype) {
        case DTYPE_FLOAT: {
            // Initialize output with first input
            float* out_data = (float*) out->data + out->head;
            float* in0_data = (float*) inputs[0]->data + inputs[0]->head;

            for (size_t i = 0; i < n_samples; i++) {
                out_data[i] = in0_data[i];
            }

            // Multiply by remaining inputs
            for (int j = 1; j < n_inputs; j++) {
                float* in_data = (float*) inputs[j]->data + inputs[j]->head;
                for (size_t i = 0; i < n_samples; i++) {
                    out_data[i] *= in_data[i];
                }
            }
            break;
        }
        case DTYPE_INT: {
            int* out_data = (int*) out->data + out->head;
            int* in0_data = (int*) inputs[0]->data + inputs[0]->head;

            for (size_t i = 0; i < n_samples; i++) {
                out_data[i] = in0_data[i];
            }

            for (int j = 1; j < n_inputs; j++) {
                int* in_data = (int*) inputs[j]->data + inputs[j]->head;
                for (size_t i = 0; i < n_samples; i++) {
                    out_data[i] *= in_data[i];
                }
            }
            break;
        }
        case DTYPE_UNSIGNED: {
            unsigned* out_data = (unsigned*) out->data + out->head;
            unsigned* in0_data = (unsigned*) inputs[0]->data + inputs[0]->head;

            for (size_t i = 0; i < n_samples; i++) {
                out_data[i] = in0_data[i];
            }

            for (int j = 1; j < n_inputs; j++) {
                unsigned* in_data =
                    (unsigned*) inputs[j]->data + inputs[j]->head;
                for (size_t i = 0; i < n_samples; i++) {
                    out_data[i] *= in_data[i];
                }
            }
            break;
        }
        default:
            SET_FILTER_ERROR(filter, Bp_EC_INVALID_DTYPE, "Unsupported dtype");
            break;
    }
}