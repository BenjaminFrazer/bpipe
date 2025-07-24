#ifndef SIGNAL_GENERATOR_H
#define SIGNAL_GENERATOR_H

#include "core.h"
#include "batch_buffer.h"
#include <stdbool.h>
#include <stdint.h>

// Waveform types supported by the signal generator
typedef enum {
    WAVEFORM_SINE,      // sin(2π * f * t + φ)
    WAVEFORM_SQUARE,    // ±1 square wave
    WAVEFORM_SAWTOOTH,  // Linear ramp -1 to +1
    WAVEFORM_TRIANGLE   // Linear up/down -1 to +1
} WaveformType_e;

// Signal generator configuration
typedef struct {
    const char* name;
    BatchBuffer_config buff_config;  // For output buffer
    long timeout_us;
    
    // Waveform parameters
    WaveformType_e waveform_type;
    double frequency_hz;         // Frequency in Hz
    double phase_rad;           // Initial phase [0, 2π]
    uint64_t sample_period_ns;  // Output sample period
    
    // Output scaling
    double amplitude;           // Peak amplitude (default 1.0)
    double offset;             // DC offset (default 0.0)
    
    // Runtime control
    uint64_t max_samples;      // 0 = unlimited
    bool allow_aliasing;       // false = error if f > Nyquist
    uint64_t start_time_ns;    // Start timestamp (default 0)
} SignalGenerator_config_t;

// Signal generator filter structure
typedef struct {
    Filter_t base;  // MUST be first member
    
    // Configuration (cached for performance)
    WaveformType_e waveform_type;
    double frequency_hz;
    double omega;              // 2π * f * 1e-9 (pre-computed)
    double initial_phase_rad;
    double amplitude;
    double offset;
    uint64_t period_ns;
    
    // Runtime state
    uint64_t next_t_ns;       // Next timestamp to generate
    uint64_t samples_generated;
    
    // Optional limits
    uint64_t max_samples;
    bool allow_aliasing;
    uint64_t start_time_ns;
    
    // Note: Using double precision time-based calculation
    // Phase accuracy degrades slowly over very long runs
    // See documentation for expected accuracy bounds
} SignalGenerator_t;

// Initialize signal generator
Bp_EC signal_generator_init(SignalGenerator_t* sg, SignalGenerator_config_t config);

#endif // SIGNAL_GENERATOR_H