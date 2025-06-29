# Python Pipeline Unit Test Specification

## Overview

A comprehensive Python-level unit test suite that validates pipeline integrity using deterministic signal patterns. Like the sawtooth demo, it leverages easily-verifiable waveforms to detect data corruption, but in an automated testing context rather than visual demonstration.

## Purpose

1. **Automated Verification**: Programmatically validate data integrity through pipelines
2. **Pattern-based Testing**: Use mathematical patterns that make corruption detectable
3. **Coverage**: Exercise all Python-accessible components in realistic scenarios
4. **Regression Prevention**: Catch data flow issues before they reach production

## Test Design Philosophy

### Core Principles

1. **Deterministic Patterns**: Use signals where corruption is mathematically verifiable
2. **No Try/Catch**: Follow project standards - let failures be explicit
3. **Pipeline-Centric**: Test complete data flows, not just individual components
4. **Fast Execution**: Complete test suite in <5 seconds

### Pattern Selection

Similar to sawtooth's visual clarity, use patterns where corruption is obvious:
- **Linear Ramp**: Detect drops, duplicates, or reordering
- **Sine Wave**: Verify smooth transformations
- **Step Function**: Test edge detection and filtering
- **Impulse Train**: Validate timing and synchronization

## Test Structure

### Test Classes

```python
# test_pipeline_integrity.py

class TestPipelineIntegrity:
    """Verify data flows through pipelines without corruption."""
    
    def test_passthrough_preserves_sawtooth(self):
        """Sawtooth through passthrough should be identical."""
        
    def test_multi_stage_pipeline(self):
        """Data integrity through 4+ stage pipeline."""
        
    def test_fan_out_consistency(self):
        """One source to multiple sinks receives identical data."""
        
    def test_fan_in_ordering(self):
        """Multiple sources to one sink maintains order."""

class TestBackpressure:
    """Verify pipeline behavior under load."""
    
    def test_slow_consumer_blocking(self):
        """Fast source with slow sink blocks appropriately."""
        
    def test_drop_mode_metrics(self):
        """Overflow=DROP mode reports correct statistics."""

class TestDataTypes:
    """Verify all supported data types flow correctly."""
    
    def test_float_pipeline(self):
        """DTYPE_FLOAT data flows without precision loss."""
        
    def test_integer_pipeline(self):
        """DTYPE_INT preserves integer values exactly."""
        
    def test_type_mismatch_detection(self):
        """Mismatched types fail at connection time."""
```

## Test Patterns

### 1. Ramp Integrity Test
```python
def test_ramp_integrity():
    """Linear ramp detects any data corruption."""
    # Generate: [0, 1, 2, 3, ...]
    # Verify: output[i] == i
    # Detects: drops, duplicates, reordering
```

### 2. Phase Coherence Test
```python
def test_phase_coherence():
    """Sine wave phase tracks continuously."""
    # Generate: sin(2πft)
    # Verify: phase increases monotonically
    # Detects: timing issues, buffer problems
```

### 3. Checksum Pipeline Test
```python
def test_checksum_preservation():
    """Running checksum validates every sample."""
    # Generate: data with running CRC
    # Verify: checksum matches at sink
    # Detects: any bit-level corruption
```

## Component Test Matrix

| Component | Test Pattern | Verification Method | Detects |
|-----------|--------------|-------------------|----------|
| Passthrough | Sawtooth | Exact match | Any modification |
| Aggregator | Counter | Sequence check | Missing samples |
| Fan-out | Impulse | All sinks match | Distribution errors |
| Type conversion | Bit pattern | Bit-exact compare | Precision loss |

## Performance Benchmarks

Each test should establish performance baselines:

```python
def test_throughput_baseline():
    """Establish minimum acceptable throughput."""
    # Target: 1M samples/second through 3-stage pipeline
    # Measure: Actual throughput
    # Assert: >= 80% of target
```

## Error Detection Patterns

### Silent Corruption Detection
```python
def generate_corruption_detector(n_samples):
    """Generate data that reveals any corruption."""
    # Include:
    # - Sample index in lower 16 bits
    # - Checksum in upper 16 bits
    # - Known pattern in middle bits
    return data

def verify_corruption_detector(data):
    """Verify no corruption occurred."""
    # Check each component independently
    # Return: (is_valid, error_description)
```

### Timing Validation
```python
def test_sample_timing():
    """Verify samples maintain consistent timing."""
    # Generate timestamps with each sample
    # Verify monotonic increase
    # Check for timing jitter
```

## Test Fixtures

### Pipeline Builders
```python
@pytest.fixture
def simple_pipeline():
    """Standard 3-stage test pipeline."""
    source = PatternSource(pattern='sawtooth')
    transform = Passthrough()
    sink = ValidatingSink(expected='sawtooth')
    # ... connect and return

@pytest.fixture
def complex_pipeline():
    """Multi-path pipeline for advanced tests."""
    # 2 sources -> transform -> 2 sinks
    # With different data rates
```

### Pattern Generators
```python
class PatternSource(BpFilterPy):
    """Generate deterministic test patterns."""
    
    patterns = {
        'sawtooth': lambda t: (t % 100) / 100,
        'sine': lambda t: np.sin(2 * np.pi * t / 100),
        'impulse': lambda t: 1.0 if t % 100 == 0 else 0.0,
        'ramp': lambda t: float(t),
        'checker': lambda t: float(t % 2)
    }
```

### Validation Sinks
```python
class ValidatingSink(BpAggregatorPy):
    """Sink that validates received pattern."""
    
    def validate(self):
        """Check if received data matches expected pattern."""
        # Return: (is_valid, error_samples, error_description)
```

## Test Execution

### Setup/Teardown
```python
def setup_method(self):
    """Fresh pipeline for each test."""
    # No global state
    # Clean shutdown verification

def teardown_method(self):
    """Verify clean shutdown."""
    # Check all threads stopped
    # No leaked resources
```

### Timeout Protection
```python
def test_with_timeout(pipeline, duration=1.0):
    """Run pipeline for fixed duration."""
    pipeline.start()
    time.sleep(duration)
    pipeline.stop()
    # Verify results
```

## Success Criteria

1. **Pattern Fidelity**: All patterns pass through pipelines unchanged
2. **Performance**: Meet throughput targets consistently
3. **Reliability**: 1000+ runs without failure
4. **Coverage**: Exercise all Python-accessible code paths
5. **Diagnostic**: Clear error messages when tests fail

## Example Test Implementation

```python
def test_sawtooth_integrity():
    """Sawtooth pattern flows through pipeline unchanged."""
    # Setup
    n_samples = 10000
    source = PatternSource('sawtooth', n_samples=n_samples)
    passthrough = Passthrough()
    sink = ValidatingSink(expected='sawtooth')
    
    # Connect
    source.add_sink(passthrough)
    passthrough.add_sink(sink)
    
    # Run
    source.start()
    passthrough.start()
    sink.start()
    
    # Wait for completion
    source.wait_complete()
    passthrough.stop()
    sink.stop()
    
    # Validate
    is_valid, errors, msg = sink.validate()
    assert is_valid, f"Pattern corruption: {msg}"
    assert sink.total_samples == n_samples
    assert sink.dropped_samples == 0
```

## Debugging Features

1. **Corruption Locator**: When test fails, report exact sample index
2. **Pattern Visualizer**: Optional plot generation for failed tests
3. **Pipeline Dump**: Save full pipeline state on failure
4. **Timing Analysis**: Report where delays occur

## Integration with CI/CD

- Run on every commit
- Performance regression detection
- Pattern library for new components
- Automated benchmark updates

## Conclusion

This test specification provides a systematic approach to validating pipeline integrity using the same principle as the sawtooth demo - easily verifiable patterns that make corruption obvious. By automating these checks, we ensure the framework maintains data fidelity across all supported use cases.