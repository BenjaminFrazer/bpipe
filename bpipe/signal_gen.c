#include "signal_gen.h"
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
