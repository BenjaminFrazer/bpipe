# Multi-Input Synchronization Specification

## Overview

This specification defines the approach for handling sample rate mismatches and timing alignment in multi-input filters within the bpipe framework. It establishes a separation of concerns where mathematical operations assume synchronized inputs, while dedicated synchronization filters handle timing alignment.

**Key Decision**: This design leverages the existing batch metadata (`t_ns`, `period_ns`, `batch_id`) rather than introducing additional timing structures. This maintains framework simplicity while providing sufficient information for synchronization.

## Design Philosophy

### Core Principles

1. **Separation of Concerns**
   - Math filters focus on computation, not timing
   - Synchronization is an explicit, composable operation
   - Simple cases remain simple, complex cases are possible

2. **Explicit Over Implicit**
   - Users explicitly add synchronization when needed
   - No hidden resampling or interpolation
   - Clear performance implications

3. **Zero-Copy by Default**
   - Synchronization filters minimize data copying
   - Direct buffer operations where possible
   - Memory allocation only when necessary (e.g., resampling)

4. **Progressive Complexity**
   - Basic operations work without timing metadata
   - Advanced features available when needed
   - Graceful degradation

### Architectural Approach

```
┌─────────┐     ┌──────────┐     ┌─────────────┐
│ Source1 │────►│  Sync    │────►│             │
│ 1000Hz  │     │  Filter  │     │   Multiply  │
├─────────┤     │          │     │   (assumes  │
│ Source2 │────►│ (aligns) │────►│synchronized)│
│ 1200Hz  │     └──────────┘     └─────────────┘
└─────────┘
```

## Implementation Phases

### Phase 1: Basic Infrastructure (Week 1-2)

**Goals:**
- Implement sample-and-hold synchronizer using existing batch metadata
- Create basic multi-input math filters
- Establish synchronization patterns

**Deliverables:**

1. **Synchronizer Using Existing Metadata**
```c
// No new structures needed - use existing Bp_Batch_t fields:
// - t_ns: timestamp of first sample in batch
// - period_ns: fixed interval (0 for irregular)
// - batch_id: sequence number for drop detection

// Synchronizer tracks state per input
typedef struct {
    long long last_t_ns;
    unsigned period_ns;
    size_t last_batch_id;
    float* last_samples;  // For sample-and-hold
    size_t n_samples;
} InputState_t;
```

2. **Basic Synchronizer**
```c
// bpipe/sync.h
typedef struct {
    Bp_Filter_t base;
    size_t n_inputs;
    InputState_t input_states[BP_MAX_INPUTS];
} Bp_SampleHoldSync_t;

Bp_EC BpSampleHoldSync_Init(Bp_SampleHoldSync_t* sync, 
                            size_t n_inputs,
                            const BpFilterConfig* config);

// Transform reads from all inputs, aligns by t_ns
void BpSampleHoldSync_Transform(Bp_Filter_t* filter,
                               Bp_Batch_t** inputs, int n_inputs,
                               Bp_Batch_t** outputs, int n_outputs);
```

3. **Multi-Input Math Operations**
```c
// bpipe/math_ops.h
typedef struct {
    Bp_Filter_t base;
    size_t n_inputs;
} Bp_MultiInputOp_t;

// Element-wise operations
Bp_EC BpAdd_Init(Bp_MultiInputOp_t* op, size_t n_inputs, const BpFilterConfig* cfg);
Bp_EC BpMultiply_Init(Bp_MultiInputOp_t* op, size_t n_inputs, const BpFilterConfig* cfg);
```

**Tests:**
- Unit tests for timing metadata
- Sample-and-hold with known patterns
- Multi-input math with synchronized data

### Phase 2: Advanced Synchronization (Week 3-4)

**Goals:**
- Implement interpolating synchronizer
- Add windowed synchronization
- Create timing analysis utilities

**Deliverables:**

1. **Interpolating Synchronizer**
```c
typedef struct {
    Bp_Filter_t base;
    InterpolationType_t method;  // LINEAR, CUBIC, ZERO_ORDER_HOLD
    long long max_extrapolation_ns;  // Max time to extrapolate
    InputState_t input_states[BP_MAX_INPUTS];
    float* history[BP_MAX_INPUTS];   // Past samples for interpolation
    size_t history_size;
} Bp_InterpolatingSync_t;

// Uses t_ns and period_ns to interpolate between samples
```

2. **Windowed Synchronizer**
```c
typedef struct {
    Bp_Filter_t base;
    long long window_duration_ns;
    AggregationMethod_t method;  // AVERAGE, MEDIAN, LAST
    long long window_start_ns;
    float* accumulators[BP_MAX_INPUTS];
    size_t sample_counts[BP_MAX_INPUTS];
} Bp_WindowedSync_t;

// Aggregates samples within time windows based on t_ns
```

3. **Timing Analysis**
```c
// Utility to analyze timing from existing batch metadata
typedef struct {
    long long min_period_ns;
    long long max_period_ns;
    long long avg_period_ns;
    bool is_regular;  // Based on period_ns consistency
    size_t dropped_batches;  // Detected via batch_id gaps
} TimingAnalysis_t;

Bp_EC BpFilter_AnalyzeTiming(Bp_Filter_t* filter, 
                            int input_idx,
                            TimingAnalysis_t* analysis);
```

**Tests:**
- Interpolation accuracy tests
- Window boundary conditions
- Rate mismatch detection

### Phase 3: Resampling and Performance (Week 5-6)

**Goals:**
- Implement high-quality resampler
- Optimize synchronizer performance
- Add timing diagnostics

**Deliverables:**

1. **Resampling Filter**
```c
typedef struct {
    Bp_Filter_t base;
    long long target_period_ns;  // Desired output period
    ResamplingKernel_t* kernel;  // Sinc, Kaiser, etc.
    size_t kernel_size;
    float* history_buffer;
    long long next_output_t_ns;  // Next output sample time
} Bp_Resampler_t;

// Generates new batches with target_period_ns timing
```

2. **Performance Optimizations**
- SIMD acceleration for interpolation
- Lock-free synchronization where possible
- Batch prefetching

3. **Diagnostics**
```c
typedef struct {
    uint64_t samples_aligned;
    uint64_t samples_interpolated;
    uint64_t samples_dropped;
    double max_time_delta;
    double avg_time_delta;
} Bp_SyncStats_t;
```

**Tests:**
- Resampling quality (SNR, aliasing)
- Performance benchmarks
- Long-running stability

## Test Strategy

### 1. Pattern-Based Validation

Use deterministic patterns that reveal synchronization errors:

```python
def test_phase_alignment():
    """Two sine waves should maintain phase relationship."""
    source1 = SineSource(freq=10, phase=0, rate=1000)
    source2 = SineSource(freq=10, phase=π/2, rate=1200)
    sync = InterpolatingSync()
    multiply = Multiply()
    
    # Connect: sources -> sync -> multiply
    # Verify: output maintains quadrature relationship
```

### 2. Edge Case Testing

```python
def test_rate_mismatch_extremes():
    """Test with extreme rate ratios."""
    # 10:1 ratio
    fast_source = SignalGen(rate=10000)
    slow_source = SignalGen(rate=1000)
    
    # Verify no buffer overflows
    # Check interpolation accuracy
```

### 3. Timing Accuracy Tests

```python
def test_timestamp_preservation():
    """Verify batch timing flows correctly."""
    # Source generates batches with specific t_ns values
    source = IrregularSource()  # period_ns = 0
    sync = WindowedSync(window_ns=100_000_000)  # 100ms windows
    
    # Verify sync correctly reads t_ns from input batches
    # Check output batches have aligned t_ns values
    # Verify batch_id sequence for drop detection
```

### 4. Performance Benchmarks

```python
def benchmark_synchronizer_overhead():
    """Measure synchronization cost."""
    configurations = [
        ("passthrough", None),
        ("sample_hold", SampleHoldSync()),
        ("interpolate", InterpolatingSync()),
        ("resample", Resampler(factor=1.2))
    ]
    
    # Measure throughput and latency
    # Profile memory usage
```

### 5. Integration Tests

```python
def test_complex_pipeline():
    """Real-world multi-rate scenario."""
    # Audio (48kHz) + Video sync (30Hz) + Control (100Hz)
    audio = AudioSource(rate=48000)
    video = VideoMetadata(rate=30)
    control = ControlSignal(rate=100)
    
    # Synchronize to audio rate
    sync = InterpolatingSync(target_rate=48000)
    
    # Verify synchronized processing
```

## How Synchronizers Use Existing Metadata

Synchronizers leverage the batch metadata to align multi-input data:

1. **Time Alignment**: Compare `t_ns` values across inputs to find corresponding samples
2. **Rate Detection**: Use `period_ns` to understand sampling rates (0 = irregular)
3. **Drop Detection**: Monitor `batch_id` sequences for missing data
4. **Interpolation**: Calculate intermediate timestamps using `t_ns + n * period_ns`

Example synchronization logic:
```c
// In synchronizer transform function
long long target_time = /* current output time */;

for (int i = 0; i < n_inputs; i++) {
    Bp_Batch_t* batch = inputs[i];
    
    // Calculate sample time within batch
    if (batch->period_ns > 0) {
        // Regular sampling: interpolate within batch
        int sample_idx = (target_time - batch->t_ns) / batch->period_ns;
        // ... interpolate if needed
    } else {
        // Irregular: each batch is one sample at t_ns
        // ... use sample-and-hold or interpolate from history
    }
}
```

## Usage Examples

### Simple Case (No Sync Needed)
```python
# When sources are already synchronized
source1 = SignalGen(rate=1000)
source2 = SignalGen(rate=1000)
multiply = Multiply()

source1.connect(multiply.input[0])
source2.connect(multiply.input[1])
```

### Sample-and-Hold Sync
```python
# Different rates, use latest value
source1 = SignalGen(rate=1000)
source2 = SignalGen(rate=750)
sync = SampleHoldSync(n_inputs=2)
multiply = Multiply()

source1.connect(sync.input[0])
source2.connect(sync.input[1])
sync.connect(multiply)
```

### Interpolating Sync
```python
# Smooth interpolation between samples
sync = InterpolatingSync(n_inputs=2, method='linear')
# ... connections ...
```

### Resampling Pipeline
```python
# Normalize to common rate
resample1 = Resampler(output_rate=1024)
resample2 = Resampler(output_rate=1024)

source1.connect(resample1)
source2.connect(resample2)
resample1.connect(multiply.input[0])
resample2.connect(multiply.input[1])
```

## Success Criteria

1. **Correctness**
   - No data corruption in synchronized streams
   - Accurate interpolation within specified bounds
   - Proper handling of edge cases

2. **Performance**
   - < 5% overhead for sample-and-hold
   - < 20% overhead for linear interpolation
   - Resampling meets real-time constraints

3. **Usability**
   - Clear error messages for timing mismatches
   - Intuitive API for common cases
   - Good defaults that work out of the box

4. **Maintainability**
   - Clean separation between sync and math
   - Well-documented timing assumptions
   - Comprehensive test coverage

## Future Extensions

1. **Adaptive Synchronization**: Automatically choose sync strategy based on rates
2. **Time-Varying Rates**: Handle sources with changing sample rates
3. **Network Time Sync**: Integration with PTP/NTP for distributed systems
4. **GPU Acceleration**: CUDA/OpenCL kernels for resampling

## Conclusion

This specification provides a flexible, performant approach to multi-input synchronization that maintains the simplicity of the bpipe framework while enabling complex multi-rate processing when needed. The phased implementation allows for incremental development and testing, ensuring stability at each stage.
