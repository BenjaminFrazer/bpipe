# Pytest Testing Guide

## Overview

The Python test suite for bpipe has been refactored to use pytest with a clean, maintainable structure following the project style guide.

## Test Organization

```
py-tests/
├── conftest.py              # Shared fixtures and pytest configuration
├── test_dpcore.py           # Core C extension (dpcore) tests
├── test_filters.py          # Python filter components tests
├── test_integration.py      # End-to-end integration tests
├── debug/                   # Archived debug scripts (not part of test suite)
└── examples/                # Example scripts demonstrating usage
```

### Test Files

- **`test_dpcore.py`**: Comprehensive tests for the dpcore C extension
  - Module constants (waveforms, data types)
  - Filter creation (BpFilterBase, BpFilterPy, BpAggregatorPy)
  - Connection/disconnection operations
  - Start/stop functionality
  - Edge cases and error handling

- **`test_filters.py`**: Tests for Python filter components
  - Signal generator creation function
  - PlotSink functionality
  - Custom filter creation with BpFilterPy
  - Filter integration scenarios

- **`test_integration.py`**: End-to-end pipeline tests
  - Complete signal processing pipelines
  - Multi-input/output configurations
  - Complex filter topologies
  - Performance characteristics

- **`conftest.py`**: Shared test infrastructure
  - Common fixtures (filters, generators, sinks)
  - Sample data generation
  - Automatic cleanup
  - Custom pytest markers

## Running Tests

### Install Dependencies
```bash
pip install -r requirements.txt
```

### Run All Tests
```bash
# Using make
make test-py

# Or directly with pytest
pytest py-tests -v
```

### Run Specific Test File
```bash
pytest py-tests/test_dpcore.py -v
```

### Run Specific Test Class or Function
```bash
# Run all tests in a class
pytest py-tests/test_filters.py::TestSignalGenerator -v

# Run specific test
pytest py-tests/test_filters.py::TestSignalGenerator::test_signal_generator_waveforms -v
```

### Run Tests with Coverage
```bash
# Using make
make test-py-coverage

# Or directly
pytest py-tests --cov=bpipe --cov-report=html --cov-report=term
```

### Skip Slow Tests
```bash
pytest py-tests -m "not slow" -v
```

## Writing New Tests

### Style Guidelines

1. **No try/except blocks** - Let pytest handle test failures
2. **Direct assertions** - Use simple `assert` statements
3. **Descriptive names** - Follow pattern: `test_<what>_<condition>_<expected>`
4. **Minimal setup** - Use fixtures for common setup
5. **No unnecessary abstraction** - Keep tests simple and readable

### Example Test Structure

```python
def test_filter_creation_with_invalid_dtype():
    """Test that invalid dtype raises appropriate error."""
    with pytest.raises(Exception):
        dpcore.BpFilterBase(capacity_exp=10, dtype=999)
```

### Using Fixtures

```python
def test_signal_pipeline(signal_generator, plot_sink):
    """Test connecting signal generator to plot sink."""
    signal_generator.add_sink(plot_sink)
    # Test logic here
    # Cleanup handled automatically by fixture
```

### Adding Markers

```python
@pytest.mark.slow
def test_long_running_pipeline():
    """Test that takes significant time."""
    # Long test here

@pytest.mark.requires_matplotlib  
def test_plotting_functionality():
    """Test that requires matplotlib."""
    # Plotting test here
```

## Configuration

Test discovery and behavior is configured in `pytest.ini`:

```ini
[pytest]
python_files = test_*.py
python_classes = Test*
python_functions = test_*
testpaths = py-tests
addopts = -v --tb=short
```

## Debugging Tests

### Run with More Verbose Output
```bash
pytest py-tests -vv
```

### Show Print Statements
```bash
pytest py-tests -s
```

### Drop into Debugger on Failure
```bash
pytest py-tests --pdb
```

### Run Last Failed Tests
```bash
pytest py-tests --lf
```

## Continuous Integration

The test suite is designed to integrate with CI/CD pipelines:

```bash
# Run all tests and fail on first error
pytest py-tests --exitfirst

# Generate XML report for CI
pytest py-tests --junit-xml=test-results.xml
```