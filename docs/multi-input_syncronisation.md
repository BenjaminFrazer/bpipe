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

## Core Alignment Primitives

### Summary

The bpipe synchronization strategy is built on seven fundamental alignment primitives, each solving a specific timing problem:

1. **Sample Phase Alignment** - Ensures timestamps align to sample grid boundaries
2. **Batch Size Matching** - Ensures all streams have identical batch sizes  
3. **Batch Phase Zeroing** - Aligns batch boundaries to epoch (t=0)
4. **Rate Conversion** - Changes sample rate while preserving signal integrity
5. **Regularization** - Converts irregular/event-driven data to regular sampling
6. **Time Window Synchronization** - Outputs only when all inputs have overlapping data
7. **Gap Filling** - Handles missing data without breaking downstream processing

### Detailed Primitive Descriptions

#### 1. Sample Phase Alignment (SampleAligner)

**Purpose**: Corrects phase offsets in regularly sampled data, ensuring all timestamps align to the sample grid.

**Problem Solved**: Sensors may start at arbitrary times, resulting in timestamps like t=12345ns when the sample period is 1000ns. This creates a 345ns phase offset that prevents proper batch alignment.

**Guarantees**:
- Output timestamps satisfy `t_ns % period_ns == 0`
- Sample rate is preserved exactly
- Signal quality maintained through configurable interpolation

**When to Use**: Before any batch alignment operations when data has non-zero phase offset.

[Detailed specification: specs/sample_aligner_design.md]

#### 2. Batch Size Matching (BatchMatcher) 

**Purpose**: Ensures multiple streams have identical batch sizes AND zero phase offset for element-wise operations.

**Problem Solved**: Element-wise operations require all inputs to have the same number of samples per batch. This filter auto-detects the required size from its connected sink.

**Guarantees**:
- Output batch size matches downstream requirements (auto-detected)
- All batches start at t = k × batch_period (phase=0)
- Zero configuration needed

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

**Guarantees**:
- Output has constant period_ns
- Configurable interpolation methods (HOLD, LINEAR)
- Handles gaps gracefully

**When to Use**: When processing event-driven or irregularly sampled data.

#### 5. Time Window Synchronization (TimeWindowSync)

**Purpose**: Ensures multiple streams only output data for time ranges where all streams have data.

**Problem Solved**: In multi-sensor systems, not all sensors may have data for all time ranges due to startup delays or dropouts.

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

## Key Principles

1. **Explicit Over Implicit**: Synchronization is visible in the pipeline
2. **Fail Fast**: Primitives error on invalid assumptions rather than hide problems
3. **Zero Configuration**: Auto-detection where possible (e.g., BatchMatcher)
4. **Composability**: Complex synchronization from simple primitives
5. **Performance**: Each primitive optimized for its specific task

## Conclusion

This specification defines a complete set of timing alignment primitives that enable complex multi-rate, multi-source telemetry processing through composition of simple, focused filters. By separating synchronization concerns from computation, the framework maintains clarity while providing the flexibility to handle real-world timing challenges.
