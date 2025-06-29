/*
 * Multi-Rate Synchronization Example
 * 
 * This example demonstrates how to use the BpZOHResampler to synchronize
 * multiple data streams with different sample rates.
 * 
 * Setup:
 * - Source 1: 1000 Hz sine wave
 * - Source 2: 1200 Hz cosine wave  
 * - Source 3: 100 Hz square wave
 * - Output: 500 Hz synchronized data
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include "core.h"
#include "signal_gen.h"
#include "resampler.h"

/* Simple sink that prints statistics */
typedef struct {
    Bp_Filter_t base;
    size_t total_samples;
    size_t batch_count;
} PrintSink_t;

void PrintSinkTransform(Bp_Filter_t* filter, Bp_Batch_t** inputs,
                       int n_inputs, Bp_Batch_t* const* outputs, int n_outputs) {
    PrintSink_t* sink = (PrintSink_t*)filter;
    Bp_Batch_t* batch = inputs[0];
    
    if (batch->tail > batch->head) {
        size_t n_samples = batch->tail - batch->head;
        sink->total_samples += n_samples;
        sink->batch_count++;
        
        /* Print first few samples of each batch */
        if (sink->batch_count <= 5) {
            printf("Batch %zu: %zu samples, t_ns=%lld, period_ns=%u\n",
                   sink->batch_count, n_samples, batch->t_ns, batch->period_ns);
            
            float* data = (float*)batch->data;
            printf("  Data: ");
            for (size_t i = 0; i < 6 && i < n_samples; i++) {
                printf("%.2f ", data[i]);
            }
            if (n_samples > 6) printf("...");
            printf("\n");
        }
    }
}

Bp_EC PrintSink_Init(PrintSink_t* sink) {
    BpFilterConfig config = BP_FILTER_CONFIG_DEFAULT;
    config.transform = PrintSinkTransform;
    config.dtype = DTYPE_FLOAT;
    config.number_of_input_filters = 1;
    
    sink->total_samples = 0;
    sink->batch_count = 0;
    
    return BpFilter_Init(&sink->base, &config);
}

int main() {
    printf("Multi-Rate Synchronization Example\n");
    printf("==================================\n\n");
    
    /* Create signal sources at different rates */
    Bp_SignalGen_t source1, source2, source3;
    
    /* Source 1: 1000 Hz sine wave */
    Bp_EC ec = BpSignalGen_Init(&source1, BP_WAVE_SINE, 
                               0.01f,    /* 10 Hz signal frequency */
                               1.0f,     /* amplitude */
                               0.0f,     /* phase */
                               0.0f,     /* offset */
                               128,      /* buffer size */
                               64,       /* batch size */
                               6);       /* n_batches_exp */
    if (ec != Bp_EC_OK) {
        printf("Failed to init source1: %d\n", ec);
        return 1;
    }
    
    /* SignalGen will generate samples with proper timing */
    
    /* Source 2: 1200 Hz cosine wave (sine with 90 degree phase) */
    ec = BpSignalGen_Init(&source2, BP_WAVE_SINE,
                         0.008333f,  /* 10 Hz at 1200 Hz sample rate */
                         2.0f,       /* amplitude */
                         0.25f,      /* 90 degree phase */
                         0.0f,       /* offset */
                         128, 64, 6);
    if (ec != Bp_EC_OK) {
        printf("Failed to init source2: %d\n", ec);
        return 1;
    }
    
    
    /* Source 3: 100 Hz square wave */
    ec = BpSignalGen_Init(&source3, BP_WAVE_SQUARE,
                         0.1f,       /* 10 Hz at 100 Hz sample rate */
                         3.0f,       /* amplitude */
                         0.0f,       /* phase */
                         0.0f,       /* offset */
                         128, 10, 6);  /* smaller batches for low rate */
    if (ec != Bp_EC_OK) {
        printf("Failed to init source3: %d\n", ec);
        return 1;
    }
    
    
    /* Create ZOH resampler for 500 Hz output */
    BpZOHResampler_t resampler;
    BpZOHResamplerConfig resamp_config = BP_ZOH_RESAMPLER_CONFIG_DEFAULT;
    resamp_config.output_period_ns = 2000000;  /* 2ms = 500 Hz */
    resamp_config.base_config.dtype = DTYPE_FLOAT;
    resamp_config.base_config.number_of_input_filters = 3;
    resamp_config.base_config.batch_size = 64;
    resamp_config.drop_on_underrun = false;  /* Hold last value */
    
    ec = BpZOHResampler_Init(&resampler, &resamp_config);
    if (ec != Bp_EC_OK) {
        printf("Failed to init resampler: %d\n", ec);
        return 1;
    }
    
    /* Create sink to display results */
    PrintSink_t sink;
    ec = PrintSink_Init(&sink);
    if (ec != Bp_EC_OK) {
        printf("Failed to init sink: %d\n", ec);
        return 1;
    }
    
    /* Connect pipeline */
    /* Sources -> Resampler -> Sink */
    ec = Bp_add_source(&resampler.base, &source1.base);
    if (ec != Bp_EC_OK) {
        printf("Failed to connect source1: %d\n", ec);
        return 1;
    }
    
    ec = Bp_add_source(&resampler.base, &source2.base);
    if (ec != Bp_EC_OK) {
        printf("Failed to connect source2: %d\n", ec);
        return 1;
    }
    
    ec = Bp_add_source(&resampler.base, &source3.base);
    if (ec != Bp_EC_OK) {
        printf("Failed to connect source3: %d\n", ec);
        return 1;
    }
    
    ec = Bp_add_sink(&resampler.base, &sink.base);
    if (ec != Bp_EC_OK) {
        printf("Failed to connect sink: %d\n", ec);
        return 1;
    }
    
    /* Start all filters */
    printf("Starting pipeline...\n");
    printf("Source rates: 1000 Hz, 1200 Hz, 100 Hz\n");
    printf("Output rate: 500 Hz\n");
    printf("Output format: interleaved [src1, src2, src3, ...]\n\n");
    
    Bp_Filter_Start(&source1.base);
    Bp_Filter_Start(&source2.base);
    Bp_Filter_Start(&source3.base);
    Bp_Filter_Start(&resampler.base);
    Bp_Filter_Start(&sink.base);
    
    /* Run for 100ms */
    usleep(100000);
    
    /* Stop filters */
    Bp_Filter_Stop(&source1.base);
    Bp_Filter_Stop(&source2.base);
    Bp_Filter_Stop(&source3.base);
    Bp_Filter_Stop(&resampler.base);
    Bp_Filter_Stop(&sink.base);
    
    /* Print statistics */
    printf("\nStatistics:\n");
    printf("Total output samples: %zu\n", sink.total_samples);
    printf("Total batches: %zu\n", sink.batch_count);
    printf("Expected samples at 500Hz for 100ms: ~%d (x3 for 3 inputs = ~%d)\n", 
           50, 150);
    
    /* Get resampler statistics */
    printf("\nResampler Input Statistics:\n");
    for (size_t i = 0; i < 3; i++) {
        BpResamplerInputStats_t stats;
        BpZOHResampler_GetInputStats(&resampler, i, &stats);
        printf("Input %zu: processed=%llu, underruns=%llu (%.1f%%)\n",
               i, (unsigned long long)stats.samples_processed,
               (unsigned long long)stats.underrun_count,
               stats.underrun_percentage);
    }
    
    /* Cleanup */
    BpFilter_Deinit(&source1.base);
    BpFilter_Deinit(&source2.base);
    BpFilter_Deinit(&source3.base);
    BpZOHResampler_Deinit(&resampler);
    BpFilter_Deinit(&sink.base);
    
    printf("\nExample completed successfully!\n");
    return 0;
}