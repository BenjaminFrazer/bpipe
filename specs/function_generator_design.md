# Function Generator Filter - Design Specification

## A) Intent

### Primary Purpose
The Function Generator is a zero-input single-output (ZISO) filter that generates discretized periodic waveforms as telemetry data sources. It acts as a signal source for testing, simulation, and signal processing applications.

### The Problem It Solves

Many telemetry processing pipelines need test signals or reference waveforms:

```
Use Cases:
1. Testing filters with known inputs
2. Generating reference signals for calibration
3. Creating synthetic data for simulation
4. Providing timing references (clock signals)

Example Pipeline:
FunctionGenerator → Filter → Analysis
       ↓
   (sine wave)
```

### Key Design Decisions

1. **Zero Inputs**: Pure source filter - generates data independently
2. **Configurable Waveforms**: Support common periodic signals
3. **Precise Timing**: Sample-accurate generation based on period_ns
4. **Time-Based Generation**: Each sample computed from time, enabling phase coherence
5. **Batch-Optimized**: Generate full batches efficiently
6. **Simple Implementation**: Use double precision for general-purpose accuracy

## B) Requirements

### Functional Requirements

#### 1. Waveform Types
```c
typedef enum {
    WAVEFORM_SINE,      // sin(2π * f * t + φ)
    WAVEFORM_SQUARE,    // ±1 square wave
    WAVEFORM_SAWTOOTH,  // Linear ramp -1 to +1
    WAVEFORM_TRIANGLE   // Linear up/down -1 to +1
} WaveformType_e;
```

#### 2. Configuration Parameters
- **waveform_type**: Type of periodic signal to generate
- **frequency_hz**: Frequency of the waveform in Hz
- **phase_rad**: Initial phase offset in radians [0, 2π]
- **sample_period_ns**: Time between samples (sets output sample rate)
- **amplitude**: Peak amplitude (default 1.0)
- **offset**: DC offset (default 0.0)

#### 3. Output Guarantees
- **Sample Rate**: Exactly `1e9 / sample_period_ns` Hz
- **Phase Continuity**: Phase coherent across batches
- **Timing Accuracy**: All timestamps aligned to sample grid
- **Amplitude Range**: Configurable, default ±1.0

### Non-Functional Requirements

1. **Performance**
   - Vectorized waveform generation where possible
   - Minimal per-sample computation
   - Pre-compute constants during initialization

2. **Accuracy**
   - Double precision floating-point for phase calculation
   - Acceptable phase accuracy for typical use cases:
     * 1 kHz at 1 MHz: < 1° error after ~30 days
     * 10 kHz at 1 MHz: < 1° error after ~3 days  
     * 1 kHz at 48 kHz: < 1° error after ~600 days
   - Time-based calculation prevents unbounded drift

## C) Challenges/Considerations

### 1. Phase Accumulation vs Time-Based Calculation

**Challenge**: Maintaining phase accuracy over millions of samples

**Option A: Phase Accumulator**
```c
phase += 2.0 * PI * frequency_hz * sample_period_s;
if (phase > 2*PI) phase -= 2*PI;  // Wrap
value = sin(phase);
```
- Pro: Simple, fast per-sample
- Con: Accumulates floating-point errors

**Option B: Time-Based (Recommended)**
```c
double t_s = t_ns * 1e-9;
double phase = 2.0 * PI * frequency_hz * t_s + initial_phase_rad;
value = sin(phase);
```
- Pro: No drift, exact phase at any time
- Con: More computation per sample

**Solution**: Use time-based calculation with optimization
```c
// Pre-compute during init
double omega = 2.0 * PI * frequency_hz * 1e-9;  // rad/ns

// Per-sample (vectorizable)
for (i = 0; i < n; i++) {
    double phase = omega * (t_ns + i * period_ns) + initial_phase_rad;
    samples[i] = amplitude * sin(phase) + offset;
}
```

### 2. Phase Precision Limitations

**Challenge**: Double precision has finite accuracy for large time values

**Analysis**: 
```c
// After extended runtime:
uint64_t t_ns = 86400ULL * 1000000000ULL;  // 1 day at 1ns resolution
double phase = omega * t_ns;  // Loses least significant bits
```

**Decision**: Accept this limitation for general-purpose use
- Document expected accuracy for common scenarios
- Most telemetry applications run for hours/days, not months
- Create specialized `IntegerSignalGenerator` if perfect phase needed

### 3. Efficient Waveform Generation

**Challenge**: Computing transcendental functions is expensive

**Solution**: Waveform-specific optimizations
```c
switch (waveform_type) {
    case WAVEFORM_SINE:
        // Accept sin() cost for accuracy, consider SIMD later
        break;
    case WAVEFORM_SQUARE:
        // Simple comparison: sin(phase) >= 0 ? 1 : -1
        break;
    case WAVEFORM_SAWTOOTH:
        // Modulo arithmetic: 2 * fmod(phase, 2*PI) / PI - 1
        break;
    case WAVEFORM_TRIANGLE:
        // Piecewise linear based on phase quadrant
        break;
}
```

### 4. Nyquist Frequency Constraints

**Challenge**: Generating frequencies above Nyquist causes aliasing

**Solution**: Validation and warnings
```c
double nyquist_hz = 0.5e9 / sample_period_ns;
if (frequency_hz > nyquist_hz) {
    // Warning: frequency exceeds Nyquist
    // Option A: Reject configuration
    // Option B: Allow but warn (for testing aliasing)
}
```

### 5. Batch Boundary Continuity

**Challenge**: Ensuring smooth transitions between batches

**Solution**: Time-based generation naturally handles this
```c
// Each batch starts exactly where previous ended
output->t_ns = last_t_ns + period_ns;
// Time-based formula ensures continuity
```

## D) Testing Strategy

### Unit Tests

```c
void test_sine_wave_generation(void) {
    // Verify sine wave has correct frequency, amplitude, phase
    // Check RMS value ≈ amplitude/√2
}

void test_phase_continuity(void) {
    // Generate multiple batches
    // Verify last sample of batch N connects smoothly to first of N+1
}

void test_frequency_accuracy(void) {
    // Generate known frequency
    // FFT and verify peak at exact frequency
}

void test_waveform_shapes(void) {
    // Verify each waveform matches mathematical definition
    // Check zero crossings, peaks, symmetry
}
```

### Integration Tests

```c
void test_with_downstream_filters(void) {
    // FunctionGenerator → FFT → verify spectrum
    // FunctionGenerator → Downsample → verify preserved
}

void test_long_duration_stability(void) {
    // Run for millions of samples
    // Verify phase accuracy within documented bounds
    // Not expecting perfect phase - just bounded error
}

void test_phase_precision_limits(void) {
    // Generate at high frequency for extended time
    // Verify phase error stays within expected bounds
    // Document actual vs theoretical limits
}
```

## E) Configuration

```c
typedef struct {
    const char* name;
    BatchBuffer_config buff_config;  // For output buffer
    
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
} FunctionGenerator_config_t;
```

## F) Implementation Structure

### State
```c
typedef struct {
    Filter_t base;
    
    // Configuration (cached for performance)
    WaveformType_e waveform_type;
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
    
    // Note: Using double precision time-based calculation
    // Phase accuracy degrades slowly over very long runs
    // See documentation for expected accuracy bounds
} FunctionGenerator_t;
```

### Core Algorithm
```c
void* function_generator_worker(void* arg) {
    FunctionGenerator_t* fg = (FunctionGenerator_t*)arg;
    
    // Validate configuration
    WORKER_ASSERT(&fg->base, fg->base.sinks[0] != NULL, Bp_EC_NO_SINK,
                  "Function generator requires connected sink");
    
    // Initialize timing
    fg->next_t_ns = 0;  // Or could use wall clock time
    
    while (atomic_load(&fg->base.running)) {
        // Check termination
        if (fg->max_samples && fg->samples_generated >= fg->max_samples) {
            break;
        }
        
        // Get output batch
        Batch_t* output = bb_get_head(fg->base.sinks[0]);
        
        // Generate samples
        size_t n_samples = bb_batch_size(fg->base.sinks[0]);
        if (fg->max_samples) {
            n_samples = MIN(n_samples, fg->max_samples - fg->samples_generated);
        }
        
        // Set batch metadata
        output->t_ns = fg->next_t_ns;
        output->period_ns = fg->period_ns;
        output->head = 0;
        output->tail = n_samples;
        output->ec = Bp_EC_OK;
        
        // Generate waveform
        float* samples = (float*)output->data;
        generate_waveform(fg, samples, n_samples, fg->next_t_ns);
        
        // Update state
        fg->next_t_ns += n_samples * fg->period_ns;
        fg->samples_generated += n_samples;
        
        // Submit batch
        Bp_EC err = bb_submit(fg->base.sinks[0]);
        if (err != Bp_EC_OK) break;
        
        // Update metrics
        fg->base.metrics.samples_processed += n_samples;
        fg->base.metrics.n_batches++;
    }
    
    // Send completion
    send_completion_to_sinks(&fg->base);
    
    return NULL;
}

static void generate_waveform(FunctionGenerator_t* fg, float* samples, 
                             size_t n, uint64_t t_start_ns) {
    switch (fg->waveform_type) {
        case WAVEFORM_SINE:
            for (size_t i = 0; i < n; i++) {
                double t_ns = t_start_ns + i * fg->period_ns;
                double phase = fg->omega * t_ns + fg->initial_phase_rad;
                samples[i] = fg->amplitude * sin(phase) + fg->offset;
            }
            break;
            
        case WAVEFORM_SQUARE:
            for (size_t i = 0; i < n; i++) {
                double t_ns = t_start_ns + i * fg->period_ns;
                double phase = fg->omega * t_ns + fg->initial_phase_rad;
                samples[i] = fg->amplitude * (sin(phase) >= 0 ? 1 : -1) + fg->offset;
            }
            break;
            
        // ... other waveforms
    }
}
```

## G) Usage Examples

### Basic Signal Generation
```c
// 1 kHz sine wave at 48 kHz sample rate
FunctionGenerator_config_t config = {
    .name = "sine_1khz",
    .waveform_type = WAVEFORM_SINE,
    .frequency_hz = 1000.0,
    .phase_rad = 0.0,
    .sample_period_ns = 20833,  // ~48 kHz
    .amplitude = 1.0,
    .buff_config = {
        .dtype = DTYPE_FLOAT,
        .batch_capacity_expo = 10,  // 1024 samples
        .ring_capacity_expo = 8
    }
};

FunctionGenerator → FFTAnalyzer
```

### Test Signal for Filter Development
```c
// Square wave for testing edge detection
config.waveform_type = WAVEFORM_SQUARE;
config.frequency_hz = 100.0;

FunctionGenerator → EdgeDetector → Counter
```

### Multi-Frequency Test Setup
```c
// Multiple generators at different frequencies
FunctionGen_1kHz  →
FunctionGen_5kHz  → Mixer → SpectrumAnalyzer
FunctionGen_10kHz →
```

### Chirp Signal (Future Extension)
```c
// Sweep frequency over time
config.sweep_start_hz = 20.0;
config.sweep_end_hz = 20000.0;
config.sweep_duration_s = 10.0;
config.sweep_type = SWEEP_LINEAR;  // or SWEEP_LOG
```

## H) Open Questions/Decision Points

### 1. Start Time Strategy
- **Option A**: Start at t=0 (deterministic, good for tests)
- **Option B**: Start at current wall time (realistic timestamps)
- **Option C**: Configurable start time

**Recommendation**: Option C with default t=0
agreed option c

### 2. Multi-Channel Support
- **Current**: Single channel output
- **Future**: Generate multiple synchronized channels?
- **Use Case**: Quadrature signals (I/Q), stereo audio

**Recommendation**: Keep single channel, use multiple filters for multi-channel
agreed keep single channel, fanout is for tee filter.

### 3. Modulation Support
- **AM/FM modulation**: Useful for communications testing
- **Noise addition**: White/pink noise options
- **Harmonics**: Add controllable harmonics

**Recommendation**: Start simple, add modulation in v2
agreed add more complex signals in future revisions

### 4. Integer Output Support
- **Current design**: Float output only
- **Alternative**: Support configurable dtype
- **Use case**: Testing integer-only pipelines

**Decision**: Start with float only, add dtype support in v2
- Keep initial implementation simple
- Most signal processing uses float anyway
- Can add integer support when needed

## Success Metrics

1. **Correctness**: Generated frequencies accurate to 0.01% or better
2. **Performance**: > 1 Gsample/s for simple waveforms on modern CPU
3. **Stability**: Phase error bounded and predictable (see accuracy specs)
4. **Usability**: Simple config for common use cases
5. **Compatibility**: Works as drop-in source for any pipeline

## Implementation Notes

- Use double precision for simplicity and good-enough accuracy
- Time-based calculation prevents unbounded drift
- Document phase accuracy limitations for long runs
- Consider specialized integer implementation only if needed
- Focus on correctness and clarity over premature optimization
