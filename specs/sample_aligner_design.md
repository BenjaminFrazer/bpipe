# Sample Aligner Filter - Design Specification

## A) Intent

### Primary Purpose
The Sample Aligner is a single-input single-output (SISO) filter that corrects phase offsets in regularly sampled data, ensuring all timestamps align to the sample grid (t_ns % period_ns == 0).

### The Problem It Solves

Many data sources start sampling at arbitrary times, resulting in timestamps that don't align to clean sample boundaries:

```
Problem:
Sensor starts at t=12345ns, period=1000ns (1kHz)
Timestamps: 12345, 13345, 14345, 15345...
Phase offset: 345ns (not on sample grid!)

Solution:
SampleAligner resamples to align with grid:
Output: 12000, 13000, 14000, 15000...
        ↓
    Now compatible with BatchMatcher!
```

Without sample alignment:
```
Data → BatchMatcher → ERROR: "Input has non-integer sample phase"
```

With sample alignment:
```
Data → SampleAligner → BatchMatcher → Success!
```

### Key Design Decisions

1. **Explicit Phase Correction**: Makes phase alignment visible in the pipeline
2. **Preserves Sample Rate**: Only adjusts phase, not frequency
3. **High-Quality Resampling**: Uses proper interpolation to minimize distortion
4. **Single Responsibility**: Only fixes phase - batch operations handled downstream

## B) Requirements

### Functional Requirements

#### 1. Input Constraints
- Input MUST have `period_ns > 0` (regular sampling)
- Input MAY have any phase offset (0 ≤ phase < period_ns)
- Input data type must support interpolation (numeric types)

#### 2. Output Guarantees
- **Sample Rate**: Preserved exactly (same period_ns)
- **Phase**: All timestamps satisfy `t_ns % period_ns == 0`
- **Alignment**: Output aligns to nearest sample grid point
- **Quality**: Minimal distortion via appropriate interpolation

#### 3. Interpolation Methods
- **NEAREST**: Round to nearest grid point (lowest latency)
- **LINEAR**: Linear interpolation between samples
- **CUBIC**: Cubic spline for smoother signals
- **SINC**: Ideal reconstruction (highest quality, most latency)

### Non-Functional Requirements

1. **Performance**
   - Latency depends on interpolation method
   - SIMD optimization for interpolation operations
   - Minimal memory allocation after initialization

2. **Accuracy**
   - Configurable interpolation quality
   - Preserves signal energy
   - No aliasing artifacts

## C) Challenges/Considerations

### 1. Initial Alignment Decision

**Challenge**: Which direction to align the first sample?
```
Input at t=12345ns, period=1000ns
Option A: Align backward to t=12000ns (extrapolation)
Option B: Align forward to t=13000ns (skip data)
```

**Solution**: Configurable alignment strategy
```c
typedef enum {
    ALIGN_NEAREST,    // Minimize time shift (default)
    ALIGN_BACKWARD,   // Preserve all data (may extrapolate)
    ALIGN_FORWARD     // Never extrapolate (may skip initial data)
} AlignmentStrategy_e;
```

### 2. Interpolation at Boundaries

**Challenge**: First/last samples may not have neighbors for interpolation

**Solution**: Configurable boundary handling
- **HOLD**: Repeat edge values
- **REFLECT**: Mirror data at boundaries
- **ZERO**: Assume zero outside bounds

### 3. Non-Numeric Data Types

**Challenge**: What about data that can't be interpolated?

**Solution**: Type validation at initialization
```c
if (!dtype_supports_interpolation(input_dtype)) {
    return Bp_EC_TYPE_ERROR;  // "SampleAligner requires numeric data"
}
```

## D) Testing Strategy

### Unit Tests

```c
void test_basic_phase_correction(void) {
    // Input: t=12345, 13345, 14345... (345ns offset)
    // Expected: t=12000, 13000, 14000... (aligned)
}

void test_various_phase_offsets(void) {
    // Test phase offsets: 0, period/4, period/2, period-1
    // Verify correct alignment for each
}

void test_interpolation_accuracy(void) {
    // Input: Known sine wave with phase offset
    // Verify: Output maintains frequency content
    // Check: SNR > threshold for each method
}
```

### Integration Tests

```c
void test_with_batch_matcher(void) {
    // Pipeline: Source → SampleAligner → BatchMatcher
    // Verify: No phase errors from BatchMatcher
}
```

## E) Configuration

```c
typedef struct {
    const char* name;
    BatchBuffer_config buff_config;
    
    // Interpolation settings
    InterpolationMethod_e method;      // NEAREST, LINEAR, CUBIC, SINC
    AlignmentStrategy_e alignment;     // NEAREST, BACKWARD, FORWARD
    BoundaryHandling_e boundary;       // HOLD, REFLECT, ZERO
    
    // For SINC method
    size_t sinc_taps;                 // Filter length (0 = auto)
    float sinc_cutoff;                // Normalized cutoff (0-1)
} SampleAligner_config_t;
```

## F) Implementation Structure

### State
```c
typedef struct {
    Filter_t base;
    
    // Configuration
    InterpolationMethod_e method;
    AlignmentStrategy_e alignment;
    
    // Runtime state
    uint64_t period_ns;              // From first input
    uint64_t next_output_ns;         // Next aligned timestamp
    
    // Interpolation buffer
    void* history_buffer;            // Previous samples for interpolation
    size_t history_size;             // Based on method
    
    // Statistics
    uint64_t samples_interpolated;
    uint64_t max_phase_correction_ns;
} SampleAligner_t;
```

### Core Algorithm
```c
void* sample_aligner_worker(void* arg) {
    SampleAligner_t* sa = (SampleAligner_t*)arg;
    
    // Initialize on first batch
    Batch_t* first = bb_get_tail(&sa->base.input_buffers[0], timeout, &err);
    sa->period_ns = first->period_ns;
    
    // Determine initial alignment
    uint64_t phase_offset = first->t_ns % sa->period_ns;
    switch (sa->alignment) {
        case ALIGN_NEAREST:
            sa->next_output_ns = (phase_offset < sa->period_ns/2) 
                ? first->t_ns - phase_offset 
                : first->t_ns + (sa->period_ns - phase_offset);
            break;
        case ALIGN_BACKWARD:
            sa->next_output_ns = first->t_ns - phase_offset;
            break;
        case ALIGN_FORWARD:
            sa->next_output_ns = first->t_ns + (sa->period_ns - phase_offset);
            break;
    }
    
    while (sa->base.running) {
        // Interpolate samples at aligned timestamps
        // Output when sufficient input available
    }
}
```

## G) Usage Example

### Standalone Phase Correction
```c
// Fix phase offset from imprecise sensor startup
SampleAligner_config_t config = {
    .name = "phase_fix",
    .method = INTERP_LINEAR,
    .alignment = ALIGN_NEAREST
};

Sensor → SampleAligner → Analysis
```

### Multi-Sensor Synchronization Pipeline
```c
// Complete synchronization with phase correction
Sensor1 → SampleAligner → BatchMatcher → TimeWindowSync → Process
Sensor2 → SampleAligner → BatchMatcher ↗
```

### High-Quality Resampling
```c
// Scientific data requiring minimal distortion
config.method = INTERP_SINC;
config.sinc_taps = 64;
config.sinc_cutoff = 0.9;

Instrument → SampleAligner → BatchMatcher → FFT
```

## Success Metrics

1. **Correctness**: Output timestamps always satisfy `t_ns % period_ns == 0`
2. **Quality**: SNR degradation < 0.1dB for LINEAR, < 0.01dB for SINC
3. **Performance**: < 5% overhead for LINEAR interpolation
4. **Compatibility**: Output works seamlessly with BatchMatcher
5. **Clarity**: Users understand when/why to use this filter