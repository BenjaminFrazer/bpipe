#include "signal_gen.h"
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Bp_EC BpSignalGen_Init(Bp_SignalGen_t *gen, BpWaveform_t waveform,
                       float frequency, float amplitude, float phase,
                       float x_offset, size_t buffer_size, int batch_size,
                       int number_of_batches_exponent)
{
    if (!gen) return Bp_EC_NOSPACE;

    // Initialize the base filter first using new config API
    BpFilterConfig config = {
        .transform = BpSignalGenTransform,
        .dtype = DTYPE_FLOAT,  // Signal generators typically output floats
        .buffer_size = buffer_size,
        .batch_size = batch_size,
        .number_of_batches_exponent = number_of_batches_exponent,
        .number_of_input_filters = 0,  // Signal generators don't need input buffers
        .overflow_behaviour = OVERFLOW_BLOCK,
        .auto_allocate_buffers = false  // Source filters don't need input buffer allocation
    };
    
    Bp_EC result = BpFilter_Init(&gen->base, &config);
    if (result != Bp_EC_OK) {
        return result;
    }

    // Initialize signal generator specific fields
    gen->waveform = waveform;
    gen->frequency = frequency;
    gen->amplitude = amplitude;
    gen->phase = phase;
    gen->x_offset = x_offset;
    gen->sample_idx = 0;

    return Bp_EC_OK;
}

static inline float fracf(float x) { return x - floorf(x); }

void BpSignalGenTransform(Bp_Filter_t *filt, Bp_Batch_t **input_batches,
                          int n_inputs, Bp_Batch_t *const *output_batches,
                          int n_outputs)
{
    (void) input_batches;
    (void) n_inputs;
    Bp_SignalGen_t *gen = (Bp_SignalGen_t *) filt;

    // Simplified: only generate signal for first output, framework handles
    // distribution
    if (n_outputs > 0 && output_batches[0] != NULL) {
        Bp_Batch_t *output_batch = output_batches[0];
        size_t space = output_batch->capacity - output_batch->head;
        float *dst_f =
            (float *) output_batch->data; /* may cast for computation */
        unsigned *dst_u = (unsigned *) output_batch->data;

        for (size_t i = 0; i < space; ++i) {
            float ph = gen->phase + gen->frequency * gen->sample_idx;
            float val = 0.0f;
            switch (gen->waveform) {
                case BP_WAVE_SQUARE:
                    val = fracf(ph) < 0.5f ? gen->amplitude : -gen->amplitude;
                    break;
                case BP_WAVE_SINE:
                    val = gen->amplitude * sinf(2.0f * (float) M_PI * ph);
                    break;
                case BP_WAVE_TRIANGLE: {
                    float fr = fracf(ph);
                    val = gen->amplitude * (4.0f * fabsf(fr - 0.5f) - 1.0f);
                    break;
                }
                case BP_WAVE_SAWTOOTH:
                default: {
                    float fr = fracf(ph);
                    val = gen->amplitude * fr;
                    break;
                }
            }
            val += gen->x_offset;
            if (filt->dtype == DTYPE_UNSIGNED) {
                dst_u[output_batch->head] = (unsigned) val;
            } else if (filt->dtype == DTYPE_INT) {
                ((int *) output_batch->data)[output_batch->head] = (int) val;
            } else {
                dst_f[output_batch->head] = val;
            }
            output_batch->head++;
            gen->sample_idx++;
        }
    }
}
