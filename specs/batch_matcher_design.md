# Batch Matcher Filter - Design Specification

## A) Intent

### Primary Purpose
The Batch Matcher is a single-input single-output (SISO) filter that performs two critical functions:
1. **Matches batch sizes** - Reframes data to match downstream consumer's requirements (auto-detected)
2. **Zeros phase** - Aligns all batches to start at t=0 + k*batch_period

These two functions together enable efficient element-wise operations between multiple streams.

### The Problem It Solves

Element-wise operations (multiply, add, subtract, etc.) require:
1. All inputs have identical batch sizes
2. All inputs have aligned batch boundaries  
3. Sample N in batch M corresponds across all inputs

Without batch matching:
```
Stream A: [0...63] [64...127] [128...191]  
Stream B: [0...127] [128...255]
         ↓
Cannot do element-wise multiply - batch sizes don't match!
```

With batch matching:
```
Stream A starts at t=12ms, 64-sample batches
Stream B starts at t=47ms, 256-sample batches

Stream A → BatchMatcher → [t=0...127] [t=128...255]   (128 samples, phase=0)
Stream B → BatchMatcher → [t=0...127] [t=128...255]   (128 samples, phase=0)
                        ↓
                   Perfect alignment for element-wise ops!
```

### Key Design Decision: Auto-Detection

The Batch Matcher automatically detects the required output batch size from its connected sink. This ensures:
- **Impossible to misconfigure** - Always matches downstream requirements
- **Single source of truth** - Downstream buffer defines alignment
- **Zero configuration** - Just connect and it works

### What It Is NOT For

1. **Arbitrary phase alignment** - Always aligns to phase=0 (epoch)
2. **Sample rate conversion** - Use Resampler filter
3. **Multi-stream synchronization** - Use TimeWindowSync filter
4. **Manual batch sizing** - Size is auto-detected from sink

## B) Requirements

### Functional Requirements

#### 1. Input Constraints
- Input MUST have `period_ns > 0` (regular sampling)
- Input timestamps MUST align to sample boundaries (t_ns % period_ns == 0)
- Input data type is preserved in output
- No assumptions about input batch size

#### 2. Output Guarantees  
- **Size**: Output batch size matches connected sink's `batch_capacity_expo`
- **Phase**: All batches start at t = k × batch_period (phase=0)
- **Alignment**: First sample of each batch has timestamp divisible by batch_period
- **Integrity**: No sample loss (except initial samples before first boundary)
- **Correctness**: No sample duplication, order preserved

#### 3. Auto-Detection Behavior
- Output batch size determined when sink is connected
- Error if sink disconnected before processing starts
- Size is immutable once processing begins

#### 4. Data Integrity
- Sample order preserved
- Timestamps preserved (adjusted to batch boundaries)
- Data values unchanged

### Non-Functional Requirements

1. **Performance**
   - Overhead < 2% vs direct memcpy
   - Memory: One accumulator buffer
   - Sequential memory access only

2. **Simplicity**
   - No user configuration beyond buffer config
   - < 200 lines of core logic
   - Clear error messages

## C) Challenges/Considerations

### 1. Connection Order Dependency

**Challenge**: Sink must be connected before starting
```c
BatchMatcher matcher;
batch_matcher_init(&matcher, config);
filt_start(&matcher);  // ERROR - no sink connected yet!
```

**Solution**: Validate in start()
```c
Bp_EC filt_start(Filter_t* filter) {
    if (filter->type == FILT_T_BATCH_MATCHER) {
        if (!filter->sinks[0]) {
            return Bp_EC_NO_SINK;  // "BatchMatcher requires connected sink"
        }
    }
}
```

### 2. Initial Alignment

**Challenge**: Input may not start at t=0
```
Input starts at t=12345ns
Output needs to align to t=0
Skip first 12345ns of data?
```

**Solution**: Verify phase alignment is valid
```
First input at t=12345ns
Sample period = 1000ns (1kHz)
Phase offset = 12345ns % 1000ns = 345ns

If phase offset ≠ 0:
    ERROR: Input has non-integer sample phase
    BatchMatcher requires input aligned to sample boundaries
    Use SampleAligner filter to correct phase offset

If phase offset = 0:
    Proceed with alignment to batch boundaries
```

### 3. Sink Buffer Changes

**Challenge**: What if sink's buffer is reconfigured?

**Solution**: Lock configuration when processing starts
- Detect size during first `filt_start()`
- Immutable thereafter
- Require restart to change alignment

## D) Alternative Approaches

### Alternative 1: Manual Configuration (Rejected)
```c
config.output_batch_size_expo = 10;  // User specifies
```
- **Cons**: Error-prone, redundant with sink config
- **Rejected because**: Auto-detection eliminates entire class of errors

### Alternative 2: Multiple Phase Support (Rejected)
```c
config.phase_ns = 12345;  // Arbitrary phase
```
- **Cons**: No benefit for element-wise ops, adds complexity
- **Rejected because**: Phase=0 works for all element-wise operations

### Alternative 3: Negotiate with Multiple Sinks (Rejected)
- **Cons**: Which sink to use? What if they differ?
- **Rejected because**: SISO keeps it simple and unambiguous

### Alternative 4: Automatic Phase Correction (Rejected)
```
Input at t=12345ns with 345ns phase offset
Automatically resample to align to sample grid
```
- **Cons**: Hidden resampling, violates single responsibility
- **Rejected because**: Phase alignment is a separate concern (use SampleAligner filter)

## E) Testing Strategy

### 1. Unit Tests

#### Basic Functionality
```c
void test_basic_batch_matching(void) {
    // Setup: Input with 64-sample batches
    // Sink: Configured for 128-sample batches
    // Expected: Output has 128-sample batches aligned to t=0
}

void test_auto_detection(void) {
    // Setup: Create matcher without specifying size
    // Connect sink with batch_capacity_expo = 8
    // Expected: Output uses 256-sample batches
}

void test_no_sink_error(void) {
    // Setup: Create matcher, don't connect sink
    // Action: Call filt_start()
    // Expected: Bp_EC_NO_SINK error
}

void test_phase_validation(void) {
    // Setup: Input with non-integer phase offset
    // Example: First sample at t=12345ns, period=1000ns
    // Phase offset = 345ns (not divisible by period)
    // Expected: Bp_EC_PHASE_ERROR - "Input has non-integer sample phase"
}
```

#### Edge Cases
```c
void test_input_already_matched(void) {
    // Input batch size = sink batch size
    // Expected: Efficient passthrough
}

void test_accumulation_across_many_batches(void) {
    // Input: 1-sample batches
    // Sink: 1024-sample batches
    // Verify: Correct accumulation without memory issues
}
```

### 2. Integration Tests
```c
void test_element_wise_pipeline(void) {
    // Pipeline:
    // Source1 → BatchMatcher → Multiply
    // Source2 → BatchMatcher ↗
    
    // Verify: Multiplication works correctly
    // Verify: Both matchers use Multiply's input buffer size
}
```

## Implementation Structure

### Configuration (Minimal!)
```c
typedef struct {
    const char* name;
    BatchBuffer_config buff_config;  // For input buffer only
} BatchMatcher_config_t;
```

### State
```c
typedef struct {
    Filter_t base;
    
    // Auto-detected configuration
    size_t output_batch_samples;    // From sink's batch_capacity_expo
    bool size_detected;             // Has sink been connected?
    
    // Runtime state
    uint64_t period_ns;             // From first input batch
    uint64_t batch_period_ns;       // period_ns * output_batch_samples
    uint64_t next_boundary_ns;      // Next output batch start time
    
    // Accumulation
    void* accumulator;              // Building current output batch
    size_t accumulated;             // Samples in accumulator
    
    // Statistics
    uint64_t samples_processed;
    uint64_t batches_matched;
} BatchMatcher_t;
```

### Connection Override
```c
// In core.c, modify filt_sink_connect:
Bp_EC filt_sink_connect(Filter_t* source, size_t output_idx, BatchBuffer_t* sink) {
    // Normal connection logic...
    
    // Special handling for BatchMatcher
    if (source->filt_type == FILT_T_BATCH_MATCHER && output_idx == 0) {
        BatchMatcher_t* matcher = (BatchMatcher_t*)source;
        matcher->output_batch_samples = 1 << sink->batch_capacity_expo;
        matcher->size_detected = true;
    }
    
    return Bp_EC_OK;
}
```

### Core Algorithm
```c
void* batch_matcher_worker(void* arg) {
    BatchMatcher_t* bm = (BatchMatcher_t*)arg;
    
    // Verify sink connected (should be checked in filt_start)
    if (!bm->size_detected) {
        bm->base.worker_err_info.ec = Bp_EC_NO_SINK;
        return NULL;
    }
    
    // Initialize on first batch
    if (!bm->period_ns) {
        Batch_t* first = bb_get_tail(&bm->base.input_buffers[0], timeout, &err);
        bm->period_ns = first->period_ns;
        
        // Validate phase alignment
        uint64_t phase_offset = first->t_ns % bm->period_ns;
        if (phase_offset != 0) {
            bm->base.worker_err_info.ec = Bp_EC_PHASE_ERROR;
            bm->base.worker_err_info.msg = "Input has non-integer sample phase. "
                                          "Use SampleAligner filter to correct phase offset.";
            bb_release_tail(&bm->base.input_buffers[0], first);
            return NULL;
        }
        
        bm->batch_period_ns = bm->period_ns * bm->output_batch_samples;
        
        // Align to t=0 (or nearest batch boundary before first sample)
        bm->next_boundary_ns = 0;
        while (bm->next_boundary_ns + bm->batch_period_ns <= first->t_ns) {
            bm->next_boundary_ns += bm->batch_period_ns;
        }
    }
    
    while (bm->base.running) {
        // Standard accumulation logic...
        // Always aligning to multiples of batch_period from t=0
    }
}
```

## Usage Example

### Before (Error-Prone)
```c
// User must manually ensure sizes match
PhaseAligner_config_t config1 = {
    .output_batch_size_expo = 10,  // Hope this matches!
    .phase_ns = 0
};
PhaseAligner_config_t config2 = {
    .output_batch_size_expo = 10,  // Hope this matches too!
    .phase_ns = 0
};

Sensor1 → PhaseAligner(config1) → Multiply
Sensor2 → PhaseAligner(config2) ↗
```

### After (Foolproof)
```c
// No size configuration needed!
BatchMatcher_config_t config = {
    .name = "auto_matcher",
    .buff_config = { /* standard buffer config */ }
};

Sensor1 → BatchMatcher → Multiply
Sensor2 → BatchMatcher ↗

// Both matchers automatically use Multiply's input buffer size
// Impossible to misconfigure!
```

## Success Metrics

1. **Zero Configuration Errors**: Impossible to misalign streams
2. **Performance**: <2% overhead vs memcpy
3. **Simplicity**: <200 lines of core logic
4. **Robustness**: Clear errors if misused
5. **User Satisfaction**: "It just works!"
