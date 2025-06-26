# Tasks
- Goal
- Considerations
- Tests

- Task specification blurb.
    - Propose implementation to achieve goal: X
    - Identify Challenges and Impacts of changes.
    - Propose alternative aproaches to achieve goal.

## PYTEST

### Goal
- add pytest unit test framework to this repository.
- migrate existing python tests to pytest.

## SOURCE_SINK_FLAG

### Goal
- add `is_source` and `is_sink` flag to core filter type.
- allows certain filters to be only inputs or outputs.

### Behaviour
- if `is_source=0` the filter's `set_sink` should fail.
- if `is_sink=0` a filter attempting to call `set_sink` on this filter as a target should fail.

## PYFILT_AGREGATOR

### Goal
- create a C based filter which will agregate each input into a seperate numpy vector.
- the numpy array for each input buffer will be stored in a python list.
- the list of arrays will be accessible as the arrays member.
- each vector in arrays will be a read-only numpy vector of the type of it's corresponding input buffer.

### Use-case
- This filter will be a generic foundation for a family of python sinks such as:
    - Matplot lib plotters.
    - python based static (non-realtime) data analysis.
    - File writers.

### Behaviour
- This filter cannot be used as a source.
- each numpy vector will be dynamically re-sized as needed up to a max capacity.
- the filter will expose the `max_capacity` parameter in it's constructor as a kwarg. Default to a nice round power of 2 around 1Gb.

## ENHANCED_AGREGATORS
1. Circular Buffer Mode
- Option to wrap around when max_capacity reached
- Useful for real-time visualization with sliding windows
2. Batch Callback API
- Allow Python callbacks on batch arrival
- Enable streaming processing without full aggregation
3. Memory-Mapped Mode
- Option to back arrays with memory-mapped files
- Handles datasets larger than RAM
4. Compression Support
- Optional compression for stored data
- Trade CPU for memory efficiency

## ABSTRACTED_BATCH_ACESSORS

### Goal
- I would preffer to abstract the interface to filters so that API's for A) retrieving data B) joining filters does not require passing pointers to buffers, rather simply reffering to the index of the input i.e. input 0, 1, 2 etc.
- 

## C_TEST_HARNESS 
- There will be many pure c based filters.
- Once initialised, much of the core interface should be symilar for every type of c filter i.e.:
    - 
- Propose Test framework for Pure C based filters.

### Goals
Create test harness for filter components which achieves the following goals:
- Clear centrelized test configuration info.
- Low maintainance. - Ideally ag


## NOISE_SOURCE - Noise source
Implement a child class of `Bp_Filter_t`  which can be used to generate noise with configurable characteristics.
- Standard deviation.
- Distribution 0=gausian.
- Cumulative density. Integral of the distribution.

