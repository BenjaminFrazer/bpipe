# Pytest Testing Guide

## Installation

Install pytest and other Python dependencies:
```bash
pip install -r requirements.txt
```

## Running Tests

### Run all tests
```bash
pytest
```

### Run tests with verbose output
```bash
pytest -v
```

### Run specific test file
```bash
pytest test_python_wrappers.py
```

### Run specific test function
```bash
pytest test_python_wrappers.py::test_filter_creation
```

## Test Organization

- `test_basic_dpcore.py` - Basic dpcore C extension functionality tests
- `test_python_wrappers.py` - Python wrapper API tests (FilterFactory, CustomFilter)

## Configuration

See `pytest.ini` for pytest configuration settings.

## Writing New Tests

Tests should follow pytest conventions:
- Test files should be named `test_*.py` or `*_test.py`
- Test functions should start with `test_`
- Use `assert` statements for test assertions
- Use `pytest.raises()` for exception testing