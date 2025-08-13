# Contract Compliance Test Suite

## Overview

The contract compliance test suite validates that filters correctly implement their declared property contracts, ensuring reliable filter composition in complex pipelines.

## Test Cases

### 1. Partial Batch Handling
**Intent**: Validate filters handle variable-sized input batches correctly.

**Method**: Generate batches with varying sizes (1, 17, 32, 64 samples) and verify the filter processes them without errors or data loss.

**Validates**: Filters don't assume full batches; handle edge cases like single-sample batches.

### 2. Data Type Compatibility
**Intent**: Ensure filters support all data types they declare in constraints.

**Method**: Test filter with DTYPE_FLOAT, DTYPE_I32, and DTYPE_U32 inputs, verifying acceptance/rejection matches declared constraints.

**Validates**: Type constraints are accurately declared and enforced.

### 3. Sample Rate Preservation
**Intent**: Verify filters handle timing metadata per their declared behavior.

**Method**: Send data with known period_ns, verify output timing matches PRESERVE/SET/SCALE behavior declarations.

**Validates**: Timing metadata is correctly propagated or transformed.

### 4. Data Integrity
**Intent**: Confirm filters preserve data values through processing.

**Method**: Process different signal patterns (sine, ramp, impulse) and verify output data matches expected transformation.

**Validates**: Core processing logic is numerically stable and deterministic.

### 5. Multi-Input Synchronization
**Intent**: Test multi-input filters handle synchronization requirements.

**Method**: Feed multiple inputs with phase offsets, verify alignment behavior matches MULTI_INPUT_ALIGNED constraints.

**Validates**: Multi-input filters correctly synchronize or process unaligned inputs.

## Outstanding Work

### Critical Fixes Needed

1. **Partial Batch Test**: 
   - ControllableProducer doesn't actually generate varying batch sizes
   - Need to implement custom batch size generation in producer

2. **Data Type Test**:
   - Dynamic dtype configuration may not work for all filters
   - Need to reinitialize filter with new config for each dtype

3. **Data Integrity Test**:
   - Only checks data flow, not actual values
   - Need to compare input/output data arrays for passthrough filters
   - Need to verify transformations for processing filters

4. **Multi-Input Sync Test**:
   - Double pthread_join causes error (already joined in filt_stop)
   - Doesn't verify actual synchronization behavior
   - Need to check output timing alignment

### Missing Test Infrastructure

1. **Property Contract Verification**:
   - Tests don't query filter's declared constraints/behaviors
   - Need to validate actual behavior matches declarations
   - Add property introspection helpers

2. **Data Validation Framework**:
   - Need helpers to compare input/output data arrays
   - Support for different validation modes (passthrough, transform, aggregate)
   - Tolerance handling for floating-point comparisons

3. **Batch Size Control**:
   - ControllableProducer needs batch_sizes array parameter
   - Or implement a VariableBatchProducer test helper

4. **Timing Verification**:
   - Need to verify period_ns in output batches
   - Check timestamp continuity across batches
   - Validate against declared SAMPLE_PERIOD behavior

### Future Enhancements

1. **Property Negotiation Tests**: Validate adaptive filters query and respond to downstream requirements

2. **Error Injection**: Test filter behavior with invalid inputs that violate constraints

3. **Performance Compliance**: Verify filters meet throughput/latency specifications

4. **Memory Compliance**: Validate filters don't leak memory or exceed buffer allocations

5. **Concurrent Access**: Test thread safety for filters with multiple outputs