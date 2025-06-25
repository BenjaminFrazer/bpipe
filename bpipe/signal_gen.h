#ifndef BPIPE_SIGNAL_GEN_H
#define BPIPE_SIGNAL_GEN_H

#include "core.h"

typedef enum {
    BP_WAVE_SQUARE = 0,
    BP_WAVE_SINE,
    BP_WAVE_TRIANGLE,
    BP_WAVE_SAWTOOTH
} BpWaveform_t;

typedef struct {
    Bp_Filter_t base;
    BpWaveform_t waveform;
    float frequency;    /* cycles per sample */
    float phase;        /* initial phase offset in cycles */
    float amplitude;
    float x_offset;
    unsigned long sample_idx;
} Bp_SignalGen_t;

Bp_EC BpSignalGen_Init(Bp_SignalGen_t* gen, BpWaveform_t waveform, float frequency, 
                       float amplitude, float phase, float x_offset, 
                       size_t buffer_size, int batch_size, int number_of_batches_exponent);

void BpSignalGenTransform(Bp_Filter_t* filt, Bp_Batch_t **input_batches, int n_inputs, Bp_Batch_t **output_batches, int n_outputs);

#endif /* BPIPE_SIGNAL_GEN_H */
