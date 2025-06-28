# UPDATE_PYTESTS Implementation Plan

## Overview
Refactor, simplify, and integrate Python unit tests to use pytest framework while adhering to the project style guide.

## Current State Analysis

### Test Organization Issues
- **Mixed test types**: Unit tests, debug scripts, and demos all in same directory
- **Inconsistent format**: Some use pytest, others use print statements
- **Path manipulation**: Tests use `sys.path.insert()` instead of proper imports
- **Redundant tests**: Multiple files testing similar functionality

### Test Files Categorization

#### Core Unit Tests (Keep & Refactor)
- `test_basic_dpcore.py` - Basic dpcore C extension functionality
- `test_python_wrappers.py` - Python wrapper API tests (already pytest format)
- `test_aggregator.py` - Aggregator functionality tests

#### Integration/Feature Tests (Consolidate)
- `test_signal_gen_integration.py` - Signal generator integration
- `test_exact_customfilter.py` - Custom filter tests
- `test_new_api.py` - API tests

#### Demo/Example Scripts (Move to examples/)
- `test_simple_demo.py` - Simple demonstration
- `test_plot_sink_simple.py` - PlotSink example

#### Debug Scripts (Already moved to debug/)
- All `debug_*.py` files
- All `test_*_debug.py` files
- `minimal_stop_test.py`

## Implementation Tasks

### Phase 1: Consolidate Core Tests
Create three main test files:

#### 1. `test_dpcore.py`
Comprehensive tests for dpcore C extension:
- [ ] Filter creation and initialization
- [ ] Data type support (DTYPE_FLOAT, DTYPE_INT, etc.)
- [ ] Buffer allocation and management
- [ ] Start/stop functionality
- [ ] Error handling and edge cases
- [ ] Memory management

#### 2. `test_filters.py`
Tests for Python filter components:
- [ ] CustomFilter creation and transform
- [ ] PlotSink functionality
- [ ] Signal generator creation helper
- [ ] Filter connection and data flow
- [ ] Error handling

#### 3. `test_integration.py`
End-to-end integration tests:
- [ ] Multi-filter pipelines
- [ ] Signal generation → processing → aggregation
- [ ] Threading and synchronization
- [ ] Performance characteristics

### Phase 2: Add Test Infrastructure

#### `conftest.py`
Common fixtures and utilities:
```python
# Fixtures for:
- Filter cleanup
- Test data generation
- Common filter configurations
- Temporary file handling
```

### Phase 3: Style Guide Compliance
- Remove all try/except blocks in tests (let pytest handle failures)
- Direct assertions without unnecessary abstraction
- Clear test names following `test_<what>_<condition>_<expected>` pattern
- Minimal setup code
- Use pytest.mark for test categorization

### Phase 4: Build System Integration

#### Makefile additions:
```makefile
# Python testing
test-py: build-py
	pytest py-tests -v

test-py-coverage: build-py
	pytest py-tests --cov=bpipe --cov-report=html

# Combined testing
test-all: test test-py
```

### Phase 5: Documentation Updates

#### Update PYTEST_README.md:
- New test organization
- How to run specific test categories
- Writing new tests guidelines
- Coverage requirements

## Test Structure After Refactoring

```
py-tests/
├── conftest.py              # Shared fixtures and utilities
├── test_dpcore.py           # Core C extension tests
├── test_filters.py          # Python filter tests  
├── test_integration.py      # Integration tests
├── debug/                   # Archived debug scripts
│   └── (debug scripts)
└── examples/                # Example scripts
    ├── simple_demo.py
    └── plot_sink_demo.py
```

## Success Criteria
- [ ] All tests run with `pytest` command
- [ ] No sys.path manipulations
- [ ] Clear separation of unit/integration/example code
- [ ] 90%+ code coverage for Python components
- [ ] Tests complete in < 5 seconds
- [ ] No redundant or obsolete tests
- [ ] Follows project style guide (no try/except, minimal abstraction)

## Migration Checklist
- [x] Move debug scripts to debug/ directory
- [x] Create consolidated test_dpcore.py
- [x] Create test_filters.py
- [x] Create test_integration.py
- [x] Add conftest.py with common fixtures
- [x] Move examples to examples/ directory
- [x] Update Makefile with pytest targets
- [x] Update PYTEST_README.md
- [x] Remove obsolete test files
- [x] Verify all tests pass
- [ ] Check test coverage
