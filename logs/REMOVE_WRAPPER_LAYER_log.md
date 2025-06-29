# REMOVE_WRAPPER_LAYER Task Log

## Summary
Removed the redundant Python wrapper layer from bpipe, exposing C extension classes directly for a cleaner, more efficient API.

## Changes Made

### 1. Simplified filters.py
- Removed BuiltinFilter and CustomFilter wrapper classes (saved ~300 lines)
- Removed FilterFactory class
- Added simple `create_signal_generator()` factory function
- Kept PlotSink as direct subclass of BpAggregatorPy

### 2. Updated bpipe/__init__.py
- Now directly imports and exports C extension classes (BpFilterBase, BpFilterPy, BpAggregatorPy)
- Exports wave constants and data type constants from dpcore
- Exports PlotSink and create_signal_generator from filters module

### 3. New API Pattern
Before (with wrapper):
```python
signal = FilterFactory.signal_generator('sawtooth', 0.01)
custom = CustomFilter(my_transform_func)
```

After (direct):
```python
signal = create_signal_generator('sawtooth', 0.01)

class MyFilter(BpFilterPy):
    def transform(self, inputs, outputs):
        # Direct implementation
```

### 4. Demo Updates
- Updated example_usage.py to show direct inheritance from BpFilterPy
- Created sawtooth_demo.py as simple example
- Created test scripts to verify functionality

### 5. Benefits Achieved
- **Less code**: ~50% reduction in Python code
- **Clearer architecture**: No hidden wrapper objects
- **Better performance**: One less function call per operation  
- **More Pythonic**: Direct inheritance instead of composition
- **Easier debugging**: Direct stack traces to C extension

## Known Issues
- Some data flow issues between filters need debugging (tracked as todo #9)
- Tests still need updating to use new API (todo #7)
- Full integration testing needed (todo #8)

## Next Steps
1. Debug and fix data flow between filters
2. Update all test files to use direct C extension classes
3. Run comprehensive tests to ensure everything works
4. Consider exposing SignalGenerator directly from C for better performance