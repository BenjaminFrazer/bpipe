# Python Integration Test Specification: Sawtooth Signal Pipeline

## Overview
This specification defines a Python integration test that verifies end-to-end data flow through a complete bpipe pipeline using Python bindings. The test creates a sawtooth signal generator, passes it through a passthrough filter, and collects the output in an aggregator to verify signal integrity.

## Objectives
1. Verify Python filter implementation and data generation
2. Test multi-stage pipeline connectivity and data flow
3. Validate signal integrity through pipeline processing
4. Ensure proper filter lifecycle management (start/stop)
5. Demonstrate Python-C interoperability in bpipe

## Pipeline Architecture
```
SawtoothGenerator → PassthroughFilter → BpAggregatorPy
     (Source)          (Processing)         (Sink)
```

## Component Specifications

### 1. SawtoothGenerator Class
- **Base Class**: `dpcore.BpFilterPy`
- **Purpose**: Generate sawtooth waveform data
- **Parameters**:
  - `frequency`: Wave frequency in Hz (default: 1.0)
  - `amplitude`: Peak amplitude (default: 1.0)
  - `sample_rate`: Samples per second (default: 1000.0)
- **Implementation**:
  - Track sample index for continuous generation
  - Generate mathematically correct sawtooth: `amplitude * (2 * ((t * frequency) % 1) - 1)`
  - Output batch size: 64 samples per transform call
  - Data type: DTYPE_FLOAT (float32)

### 2. PassthroughFilter Class
- **Base Class**: `dpcore.BpFilterPy`
- **Purpose**: Pass data unchanged while tracking statistics
- **Attributes**:
  - `samples_processed`: Counter for verification
  - `last_batch_size`: Size of last processed batch
- **Implementation**:
  - Copy input[0] to output[0] with proper bounds checking
  - Update statistics on each transform call

### 3. Test Functions

#### test_sawtooth_pipeline_basic()
- Create pipeline components
- Connect: generator → passthrough → aggregator
- Run for 1 second
- Verify:
  - Data was received (non-zero length)
  - Passthrough processed samples
  - Data maintains float32 type

#### test_sawtooth_waveform_integrity()
- Run pipeline and collect data
- Verify mathematical properties:
  - Linear ramp within each period
  - Sharp drops at period boundaries
  - Correct frequency (using FFT or zero-crossing analysis)
  - Amplitude within expected range

#### test_different_parameters()
- Test matrix of parameters:
  - Frequencies: [0.1, 1.0, 10.0, 100.0] Hz
  - Amplitudes: [0.5, 1.0, 2.0, 10.0]
- Verify each configuration works correctly

#### test_pipeline_restart()
- Start pipeline, collect data
- Stop pipeline
- Restart with same components
- Verify continues working correctly

## Data Verification Methods

### 1. Basic Verification
```python
def verify_basic_data(data):
    assert len(data) > 0
    assert data.dtype == np.float32
    assert not np.all(data == 0)
```

### 2. Sawtooth Pattern Verification
```python
def verify_sawtooth_pattern(data, frequency, amplitude, sample_rate):
    # Check amplitude range
    assert np.min(data) >= -amplitude * 1.1
    assert np.max(data) <= amplitude * 1.1
    
    # Find rising edges (sharp drops)
    diffs = np.diff(data)
    drops = np.where(diffs < -amplitude)[0]
    
    # Verify period between drops
    if len(drops) > 1:
        periods = np.diff(drops)
        expected_period = sample_rate / frequency
        assert np.all(np.abs(periods - expected_period) < expected_period * 0.1)
```

### 3. Frequency Analysis
```python
def verify_frequency(data, expected_freq, sample_rate):
    # Use FFT to find dominant frequency
    fft = np.fft.fft(data)
    freqs = np.fft.fftfreq(len(data), 1/sample_rate)
    
    # Find peak frequency
    peak_idx = np.argmax(np.abs(fft[:len(fft)//2]))
    peak_freq = freqs[peak_idx]
    
    # Allow 10% tolerance
    assert abs(peak_freq - expected_freq) < expected_freq * 0.1
```

## Error Handling

### Expected Errors
1. **RuntimeError**: When removing non-existent sink
2. **ValueError**: Invalid parameters (negative frequency, etc.)

### Timeout Protection
- Each test should complete within 5 seconds
- Use pytest timeout marker: `@pytest.mark.timeout(5)`

## Test File Structure

```python
#!/usr/bin/env python3
"""Integration test for sawtooth signal pipeline."""

import time
import numpy as np
import pytest
import dpcore

class SawtoothGenerator(dpcore.BpFilterPy):
    ...

class PassthroughFilter(dpcore.BpFilterPy):
    ...

def verify_sawtooth_pattern(data, frequency, amplitude, sample_rate):
    ...

def test_sawtooth_pipeline_basic():
    ...

def test_sawtooth_waveform_integrity():
    ...

def test_different_parameters():
    ...

def test_pipeline_restart():
    ...
```

## Success Criteria

1. All tests pass without segfaults or hangs
2. Sawtooth waveform maintains mathematical properties through pipeline
3. Memory usage remains stable (no leaks)
4. Pipeline can be started/stopped/restarted reliably
5. Data flows continuously without gaps or corruption

## Future Enhancements

1. Add performance benchmarking
2. Test with multiple parallel pipelines
3. Add stress testing with high frequencies
4. Test integration with C-based filters
5. Add visualization of test results

## Dependencies

- Python 3.8+
- numpy
- pytest
- dpcore (bpipe C extension)
- bpipe Python module

## Test Execution

```bash
# Run specific test
pytest py-tests/test_sawtooth_integration.py -v

# Run with timeout protection
pytest py-tests/test_sawtooth_integration.py -v --timeout=30

# Run specific test function
pytest py-tests/test_sawtooth_integration.py::test_sawtooth_pipeline_basic -v
```