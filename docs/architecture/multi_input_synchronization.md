# Multi-Input Synchronization Specification

## Overview

This specification defines the approach for handling sample rate mismatches and timing alignment in multi-input filters within the bpipe framework. It establishes a separation of concerns where mathematical operations assume synchronized inputs, while dedicated synchronization filters handle timing alignment.

**Key Decision**: This design leverages the existing batch metadata (`t_ns`, `period_ns`, `batch_id`) rather than introducing additional timing structures. This maintains framework simplicity while providing sufficient information for synchronization.

## The Fundamental Challenge

Many telemetry operations require processing multiple data streams together:
- Cross-correlation between sensors
- Sensor fusion algorithms  
- Differential measurements
- Multi-channel FFTs

These operations assume that:
1. Samples at index N in each stream represent the same instant in time
2. All inputs have the same sample rate
3. Batch boundaries align across streams

Real-world data rarely meets these requirements naturally.

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

## Core Alignment Primitives

### Summary

The bpipe synchronization strategy is built on six fundamental alignment primitives, each solving a specific timing problem:

1. **Sample Phase Alignment** - Ensures timestamps align to sample grid boundaries
2. **Batch Size Matching & Phase Zeroing** - Ensures identical batch sizes AND aligns to epoch (t=0)
3. **Rate Conversion** - Changes sample rate while preserving signal integrity
4. **Regularization** - Converts irregular/event-driven data to regular sampling
5. **Time Window Synchronization** - Outputs only when all inputs have overlapping data
6. **Gap Filling** - Handles missing data without breaking downstream processing

### Detailed Primitive Descriptions

#### 1. Sample Phase Alignment (SampleAligner)

**Purpose**: Corrects phase offsets in regularly sampled data, ensuring all timestamps align to the sample grid.

**Problem Solved**: Sensors may start at arbitrary times, resulting in timestamps like t=12345ns when the sample period is 1000ns. This creates a 345ns phase offset that prevents proper batch alignment.

**Example**:
```
Problem:
Sensor starts at t=12345ns with 1kHz sampling:
Timestamps: 12345, 13345, 14345, 15345...
Phase offset: 345ns (not on sample grid!)

Solution:
SampleAligner interpolates to grid-aligned timestamps:
Output: 12000, 13000, 14000, 15000...
        ↓
    Now compatible with BatchMatcher!
```

**Guarantees**:
- Output timestamps satisfy `t_ns % period_ns == 0`
- Sample rate is preserved exactly
- Signal quality maintained through configurable interpolation

**When to Use**: Before any batch alignment operations when data has non-zero phase offset.

[Detailed specification: specs/sample_aligner_design.md]

#### 2. Batch Size Matching (BatchMatcher) 

**Purpose**: Ensures multiple streams have identical batch sizes AND zero phase offset for element-wise operations.

**Problem Solved**: Element-wise operations require all inputs to have the same number of samples per batch. This filter auto-detects the required size from its connected sink.

**Example**:
```
Problem:
Two 1kHz sensors with different start times and batch sizes:
Sensor A: starts t=12ms, 64-sample batches:  [12...75] [76...139]
Sensor B: starts t=47ms, 256-sample batches: [47...302] [303...558]

Solution:
BatchMatcher performs two functions:
1. Matches batch size (auto-detected from sink): 128 samples
2. Zeros phase (aligns to t=0):

Both sensors → BatchMatcher → [t=0...127] [t=128...255] [t=256...383]
                            ↓
                     Perfect size AND phase alignment!
```

**Guarantees**:
- Output batch size matches downstream requirements (auto-detected)
- All batches start at t = k × batch_period (phase=0)
- Zero configuration needed
- No sample loss (except initial samples before t=0)

**When to Use**: Before any element-wise operation on multiple streams.

[Detailed specification: specs/batch_matcher_design.md]

#### 3. Rate Conversion (Resampler)

**Purpose**: Changes the sample rate of already-regular data using high-quality filtering.

**Problem Solved**: Different sensors may operate at different rates (e.g., 48kHz audio and 44.1kHz processing).

**Guarantees**:
- Configurable filter quality (passband ripple, stopband attenuation)
- No aliasing for downsampling
- Maintains precise timing relationships

**When to Use**: When inputs have different sample rates that need to be unified.

#### 4. Regularization (Regularizer)

**Purpose**: Converts irregular or event-driven data to fixed-rate streams.

**Problem Solved**: Many sensors produce data at irregular intervals (GPS, user events) but downstream processing expects regular sampling.

**Example**:
```
Problem:
GPS data arrives irregularly:
t=0.000s, t=0.987s, t=2.013s, t=2.891s, t=4.102s...

Solution:
Regularizer(1Hz, HOLD) produces:
t=0.0s, t=1.0s, t=2.0s, t=3.0s, t=4.0s...
```

**Guarantees**:
- Output has constant period_ns
- One sample per output batch (for downstream flexibility)
- Configurable interpolation methods (HOLD, LINEAR)
- Handles gaps gracefully

**When to Use**: When processing event-driven or irregularly sampled data.

#### 5. Time Window Synchronization (TimeWindowSync)

**Purpose**: Ensures multiple streams only output data for time ranges where all streams have data.

**Problem Solved**: In multi-sensor systems, not all sensors may have data for all time ranges due to startup delays or dropouts.

**Example**:
```
Problem:
After phase alignment, one stream has a gap:
Stream A: [t=0...99ms] [t=100...199ms] [t=200...299ms]
Stream B: [t=0...99ms] [missing]      [t=200...299ms]

Solution:
TimeWindowSync outputs:
Stream A: [t=0...99ms] [empty]        [t=200...299ms]
Stream B: [t=0...99ms] [empty]        [t=200...299ms]
```

**Guarantees**:
- Output batches have identical timestamps across all streams
- Truncates to overlapping time ranges
- Maintains sample correspondence

**When to Use**: When you need guaranteed data availability across all inputs.

#### 6. Gap Filling (GapFiller)

**Purpose**: Handles temporary data dropouts by interpolating missing samples.

**Problem Solved**: Network issues or sensor faults can cause data gaps that would break downstream processing.

**Guarantees**:
- Continuous output even with input gaps
- Configurable interpolation strategies
- Metadata flags for interpolated data

**When to Use**: In systems where temporary data loss is expected but processing must continue.

## Composition Patterns

### Basic Multi-Sensor Synchronization
```
Sensor1 → BatchMatcher → TimeWindowSync → ElementWise
Sensor2 → BatchMatcher ↗
```

### Multi-Rate Sensor Fusion
```
GPS(1Hz) → Regularizer → Resampler → BatchMatcher → TimeWindowSync → Fusion
IMU(100Hz) → Resampler → BatchMatcher ──────────────↗
Mag(10Hz) → Resampler → BatchMatcher ──────────────↗
```

### Phase-Misaligned Sensors
```
Sensor1 → SampleAligner → BatchMatcher → TimeWindowSync → Process
Sensor2 → SampleAligner → BatchMatcher ↗
```

### Robust Network Telemetry
```
Sensor1 → GapFiller → BatchMatcher → TimeWindowSync → Process
Sensor2 → GapFiller → BatchMatcher ↗
```

### Irregular to Regular Processing
```
Event1 → Regularizer → BatchMatcher → TimeWindowSync → Correlate
Event2 → Regularizer → BatchMatcher ↗
```

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

## Design Decisions

### Why Separate Synchronization Filters?

The alternative of embedding synchronization logic in each mathematical filter was rejected because:

1. **Complexity Explosion**: Every filter would need timing logic
2. **Duplication**: Same synchronization code repeated everywhere
3. **Testing Difficulty**: Hard to isolate timing vs computation bugs
4. **Performance**: Synchronization overhead even when not needed

### Why Atomic Primitives?

A monolithic \"super synchronizer\" handling all cases was rejected because:

1. **Configuration Complexity**: Too many parameters for simple cases
2. **Code Size**: Single filter would be ~1000+ lines
3. **Testing**: Exponential test combinations
4. **Performance**: Features you don't use still cost cycles

The atomic approach provides:
- Each primitive is ~200 lines of focused code
- Linear testing complexity
- Pay only for transformations you need
- Clear composition patterns

## Implementation Guidelines

### Filter Order Matters

Always apply filters in this order:
1. **Regularizer** (if dealing with irregular data)
2. **Resampler** (if rate conversion needed)
3. **SampleAligner** (if phase correction needed)
4. **GapFiller** (if dropouts expected)
5. **BatchMatcher** (to match batch sizes)
6. **TimeWindowSync** (to ensure temporal correspondence)

### Common Pitfalls

#### 1. Forgetting Batch Matching
```
// BAD: Direct to element-wise op
Sensor1 → TimeWindowSync → Multiply
Sensor2 ↗

// GOOD: Match batch sizes first
Sensor1 → BatchMatcher → TimeWindowSync → Multiply
Sensor2 → BatchMatcher ↗
```

#### 2. Wrong Filter Order
```
// BAD: Batch match before resampling
Irregular → BatchMatcher → Regularizer

// GOOD: Regularize first
Irregular → Regularizer → BatchMatcher
```

#### 3. Mismatched Expectations
```
// BAD: Expecting TimeWindowSync to interpolate
Stream1 → TimeWindowSync
Stream2 ↗

// GOOD: Handle gaps explicitly
Stream1 → GapFiller → TimeWindowSync
Stream2 → GapFiller ↗
```

#### 4. Phase-Misaligned Data
```
// BAD: Direct to BatchMatcher with phase offset
Sensor → BatchMatcher  // ERROR: "Input has non-integer sample phase"

// GOOD: Correct phase first
Sensor → SampleAligner → BatchMatcher
```

## Performance Characteristics

### Latency per Primitive
- **SampleAligner**: < period_ns (phase correction only)
- **BatchMatcher**: ≤ batch_period_ns (accumulation time)
- **Resampler**: ~filter_taps × period_ns (filter group delay)
- **Regularizer**: ≤ output_period_ns
- **TimeWindowSync**: ~0 (passthrough when aligned)
- **GapFiller**: ~0 (minimal buffering)

### Memory Requirements
- **SampleAligner**: history_size × sizeof(sample)
- **BatchMatcher**: 2 × batch_size × sizeof(sample)
- **Resampler**: filter_taps × sizeof(sample) × channels
- **Regularizer**: 2 samples (previous + current)
- **TimeWindowSync**: Minimal (pointers only)
- **GapFiller**: max_gap_samples × sizeof(sample)

### Throughput Analysis
| Filter | Operations per Sample | Bottleneck |
|--------|---------------------|------------|
| Regularizer | 1-2 (hold) or 4-6 (linear) | Memory bandwidth |
| Resampler | ~taps (convolution) | Computation |
| SampleAligner | 2-4 (interpolation) | Memory bandwidth |
| BatchMatcher | 1 (memcpy) | Memory bandwidth |
| TimeWindowSync | 1 (pointer ops) | Synchronization |

## Key Principles

1. **Explicit Over Implicit**: Synchronization is visible in the pipeline
2. **Fail Fast**: Primitives error on invalid assumptions rather than hide problems
3. **Zero Configuration**: Auto-detection where possible (e.g., BatchMatcher)
4. **Composability**: Complex synchronization from simple primitives
5. **Performance**: Each primitive optimized for its specific task

## Future Extensions

### Planned Filters
1. **ClockDomainCrossing**: Handle streams from different clock sources
2. **AdaptiveResampler**: Adjust rate based on measured clock drift
3. **QualityMonitor**: Track synchronization quality metrics
4. **Accumulator**: Combine single-sample batches into larger ones
5. **QualityIndicator**: Flag interpolated vs actual samples

### Advanced Patterns
1. **Cascaded Synchronization**: Sync groups, then sync groups-of-groups
2. **Dynamic Routing**: Choose sync strategy based on data availability
3. **Predictive Sync**: Use ML to predict missing samples
4. **GPU Acceleration**: For high-channel-count scenarios
5. **Pipeline Builder**: Automatic filter insertion based on requirements

## Conclusion

This specification defines a complete set of timing alignment primitives that enable complex multi-rate, multi-source telemetry processing through composition of simple, focused filters. By separating synchronization concerns from computation, the framework maintains clarity while providing the flexibility to handle real-world timing challenges.
