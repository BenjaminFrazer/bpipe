# Tasks
- Goal
- Considerations
- Tests

- Task specification blurb.
    - Propose implementation to achieve goal: X
    - Identify Challenges and Impacts of changes.
    - Propose alternative aproaches to achieve goal.



## AGREGATOR_TIMESTAMP_SUPPORT
- Update to aggregator to support time-stamped data.
- Agregator still retuns single dimentional contiguous array.
- supports returning ts_start and sample_period for each vector.
- Alligned data is assumed and obligation is on user to ensure this.
- This means that a normal agregator can simply retain the sample_period and time-stamp of the first batch and check subsiquent batches for equal spacing.

## AGREGATOR_TIMESTAMP_SUPPORT
- Same requirements as task:PYFILT_AGREGATOR but add the requirement to save time-stamps with every sample
- two contiguous arrays of equal length
    - uint64_t time-stamp in nanoseconds
    - array of either 4 or 8 bytes per sample depending on data width
- brain-storm clean way to implement with existing agregator.


## ARCHITECTURE_IMPROVEMENTS

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
5. PANDAS_SINK
- data is read only
- all transforms copy the data
- does not support the inplace method
6. FILE_SINKS
- CSV
- PARQUET_ARROW


## C_TEST_HARNESS 
- There will be many pure c based filters.
- Once initialised, much of the core interface should be symilar for every type of c filter i.e.:
    - 
- Propose Test framework for Pure C based filters.

### Goals
Create test harness for filter components which achieves the following goals:
- Clear centrelized test configuration info.
- Low maintainance. - Ideally ag


## NOISE_SOURCE
Implement a child class of `Bp_Filter_t`  which can be used to generate noise with configurable characteristics.
- Standard deviation.
- Distribution 0=gausian.
- Cumulative density. Integral of the distribution.

