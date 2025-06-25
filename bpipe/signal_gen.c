#include "signal_gen.h"
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Bp_EC BpSignalGen_Init(Bp_SignalGen_t* gen, BpWaveform_t waveform, float frequency, 
                       float amplitude, float phase, float x_offset, 
                       size_t buffer_size, int batch_size, int number_of_batches_exponent) {
    if (!gen) return Bp_EC_NOSPACE;
    
    // Initialize the base filter first
    Bp_EC result = BpFilter_Init(&gen->base, BpSignalGenTransform, 0, 
                                buffer_size, batch_size, number_of_batches_exponent, 0);
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
    
    // Set up the base filter properties for signal generation
    gen->base.dtype = DTYPE_FLOAT;  // Signal generators typically output floats
    gen->base.data_width = sizeof(float);
    gen->base.has_input_buffer = false;  // Signal generators don't need input
    
    return Bp_EC_OK;
}

static inline float fracf(float x)
{
    return x - floorf(x);
}

void BpSignalGenTransform(Bp_Filter_t* filt, Bp_Batch_t *input_batch, Bp_Batch_t *output_batch)
{
    (void)input_batch;
    Bp_SignalGen_t* gen = (Bp_SignalGen_t*)filt;

    size_t space = output_batch->capacity - output_batch->head;
    float *dst_f = (float*)output_batch->data; /* may cast for computation */
    unsigned *dst_u = (unsigned*)output_batch->data;

    for(size_t i=0; i<space; ++i){
        float ph = gen->phase + gen->frequency * gen->sample_idx;
        float val = 0.0f;
        switch(gen->waveform){
            case BP_WAVE_SQUARE:
                val = fracf(ph) < 0.5f ? gen->amplitude : -gen->amplitude;
                break;
            case BP_WAVE_SINE:
                val = gen->amplitude * sinf(2.0f * (float)M_PI * ph);
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
        if(filt->dtype == DTYPE_UNSIGNED){
            dst_u[output_batch->head] = (unsigned)val;
        } else if(filt->dtype == DTYPE_INT){
            ((int*)output_batch->data)[output_batch->head] = (int)val;
        } else {
            dst_f[output_batch->head] = val;
        }
        output_batch->head++;
        gen->sample_idx++;
    }
}
