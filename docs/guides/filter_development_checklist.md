# Filter Development Checklist

A comprehensive checklist for implementing new filters in bpipe2. Follow these steps to ensure your filter is correctly implemented.

## Pre-Development

- [ ] Read core documentation:
  - [ ] `docs/architecture/core_data_model.md` - Understand data structures
  - [ ] `docs/guides/filter_development_guide.md` - Learn filter patterns
  - [ ] `docs/reference/public_api_reference.md` - API conventions
  - [ ] `docs/architecture/threading_model.md` - Threading requirements
  - [ ] `docs/architecture/error_handling_guide.md` - Error patterns

- [ ] Determine filter type:
  - [ ] `FILT_T_SOURCE` - No inputs, generates data
  - [ ] `FILT_T_MAP` - Transforms input to output
  - [ ] `FILT_T_SINK` - Consumes data, no outputs
  - [ ] `FILT_T_MULTI` - Multiple inputs/outputs

- [ ] Design configuration structure:
  - [ ] Define all parameters needed
  - [ ] Document valid ranges
  - [ ] Consider future extensibility

## Implementation

### Header File (bpipe/my_filter.h)

- [ ] Include guards: `#ifndef BPIPE_MY_FILTER_H`
- [ ] Include required headers
- [ ] Define configuration structure:
  ```c
  typedef struct {
      // Parameters with clear types and units
      double frequency_hz;
      uint64_t period_ns;
      size_t n_channels;
  } MyFilterCfg_t;
  ```
- [ ] Define filter structure:
  ```c
  typedef struct {
      Filter_t base;  // MUST be first member
      // Configuration (immutable after init)
      double frequency_hz;
      uint64_t period_ns;
      // State (mutable, consider thread safety)
      atomic_size_t samples_processed;
      uint64_t next_timestamp_ns;
  } MyFilter_t;
  ```
- [ ] Declare public functions:
  ```c
  Bp_EC my_filter_init(MyFilter_t* filter, MyFilterCfg_t cfg);
  void my_filter_deinit(MyFilter_t* filter);
  ```

### Implementation File (bpipe/my_filter.c)

#### Initialization Function

- [ ] Validate all inputs:
  ```c
  if (!filter) return Bp_EC_NULL_PTR;
  if (cfg.frequency_hz <= 0) return Bp_EC_INVALID_ARG;
  ```
- [ ] Check Nyquist frequency if applicable:
  ```c
  double nyquist_hz = 1e9 / (2.0 * cfg.period_ns);
  if (cfg.frequency_hz > nyquist_hz) return Bp_EC_INVALID_ARG;
  ```
- [ ] Initialize base filter:
  ```c
  Bp_EC err = filter_init(&filter->base, "my_filter", 
                         FILT_T_MAP, n_sources, n_sinks);
  if (err != Bp_EC_OK) return err;
  ```
- [ ] Store configuration (make copies, don't store pointers)
- [ ] Initialize state variables
- [ ] Set worker function if needed:
  ```c
  filter->base.worker = my_filter_worker;
  ```
- [ ] Initialize synchronization primitives if needed

#### Worker Thread Function

- [ ] Start with null check:
  ```c
  static void* my_filter_worker(void* arg) {
      MyFilter_t* filter = (MyFilter_t*)arg;
      BP_WORKER_ASSERT(filter != NULL);
  ```
- [ ] Main processing loop:
  ```c
  while (atomic_load(&filter->base.running)) {
      // Process data...
  }
  ```
- [ ] Handle input batches (if applicable):
  ```c
  Batch_t* input = bb_get_tail(filter->base.sources[0]);
  if (!input) {
      usleep(1000);  // Avoid busy-waiting
      continue;
  }
  ```
- [ ] Handle output batches:
  ```c
  Batch_t* output = bb_get_head(filter->base.sinks[0]);
  if (!output) {
      // Return input if held
      if (input) bb_return(filter->base.sources[0], 0);
      usleep(1000);
      continue;
  }
  ```
- [ ] Set batch metadata:
  ```c
  output->t_ns = calculate_timestamp();
  output->period_ns = filter->period_ns;
  output->head = 0;
  output->tail = n_samples;
  output->ec = Bp_EC_OK;
  ```
- [ ] Process data
- [ ] Submit output batch:
  ```c
  Bp_EC err = bb_submit(filter->base.sinks[0], 0);
  BP_WORKER_ASSERT(err == Bp_EC_OK);
  ```
- [ ] Return input batch if applicable:
  ```c
  err = bb_return(filter->base.sources[0], 0);
  BP_WORKER_ASSERT(err == Bp_EC_OK);
  ```
- [ ] Check termination conditions:
  ```c
  if (should_terminate(filter)) {
      atomic_store(&filter->base.running, false);
      break;
  }
  ```

#### Deinitialization Function

- [ ] Check for null
- [ ] Clean up allocated resources
- [ ] Destroy synchronization primitives
- [ ] Call base deinit:
  ```c
  filter_deinit(&filter->base);
  ```

## Build System Integration

### Update Makefile

- [ ] Add source file to SRC_FILES:
  ```makefile
  SRC_FILES = \
      bpipe/core.c \
      bpipe/my_filter.c \
      ...
  ```
- [ ] Add header to installation if public

### Update Python Bindings (if applicable)

- [ ] Add to `python/src/bpipe_module.c`
- [ ] Create Python wrapper class
- [ ] Add to `__init__.py`

## Testing

### Create Test File (tests/test_my_filter.c)

- [ ] Include Unity and required headers
- [ ] Implement setUp/tearDown if needed
- [ ] Test initialization:
  - [ ] Valid configuration
  - [ ] Invalid configurations
  - [ ] Null pointer handling
  - [ ] Boundary conditions
- [ ] Test data processing:
  - [ ] Basic functionality
  - [ ] Edge cases (empty batches, max sizes)
  - [ ] Error conditions
- [ ] Test threading:
  - [ ] Start/stop cycles
  - [ ] Worker error handling
  - [ ] Concurrent access if applicable
- [ ] Test cleanup:
  - [ ] Resource deallocation
  - [ ] No memory leaks

### Update Build

- [ ] Add test to Makefile:
  ```makefile
  TEST_TARGETS = \
      build/test_my_filter \
      ...
  ```
- [ ] Add test rule:
  ```makefile
  test-my-filter: build/test_my_filter
  	./scripts/run_with_timeout.sh 30 ./build/test_my_filter
  ```

## Documentation

- [ ] Add inline documentation:
  ```c
  /**
   * Initialize a MyFilter instance.
   * 
   * @param filter Pointer to filter structure
   * @param cfg Configuration parameters
   * @return Bp_EC_OK on success, error code on failure
   * 
   * Possible errors:
   * - Bp_EC_NULL_PTR: filter is NULL
   * - Bp_EC_INVALID_ARG: invalid configuration
   */
  ```
- [ ] Document thread safety:
  ```c
  // Thread-safe: can be called from any thread
  // Worker thread: modifies samples_processed
  // Main thread: reads configuration
  ```
- [ ] Add usage example in header
- [ ] Update relevant documentation files

## Validation

### Code Quality

- [ ] Run linting: `make lint`
- [ ] Fix any warnings
- [ ] Check formatting: `make clang-format-check`
- [ ] Run static analysis: `make clang-tidy-check`

### Runtime Testing

- [ ] Run unit tests: `make test-my-filter`
- [ ] Check for memory leaks with valgrind
- [ ] Test with different batch sizes
- [ ] Test error conditions
- [ ] Verify thread safety

### Integration Testing

- [ ] Test in pipeline with other filters
- [ ] Test multiple instances
- [ ] Test under load
- [ ] Test shutdown scenarios

## Common Pitfalls to Avoid

- [ ] **Missing bb_submit()**: Always submit output batches
- [ ] **Wrong filter type**: Use correct FILT_T_* enum
- [ ] **Not checking worker errors**: Check worker_err_info.ec after stop
- [ ] **Busy waiting**: Use usleep() when waiting
- [ ] **Not checking running flag**: Check atomic_load(&running) in loops
- [ ] **Memory leaks**: Free all allocated memory in deinit
- [ ] **Race conditions**: Use proper synchronization
- [ ] **Forgetting metadata**: Set all batch fields (t_ns, period_ns, etc.)
- [ ] **Integer overflow**: Validate time calculations
- [ ] **Floating point precision**: Document expected precision limits

## Final Review

- [ ] Code follows project conventions
- [ ] All tests pass
- [ ] No memory leaks
- [ ] Documentation is complete
- [ ] Error handling is comprehensive
- [ ] Thread safety is ensured
- [ ] Performance is acceptable

## Example Implementation Reference

See these filters for good examples:
- `bpipe/signal_generator.c` - Zero-input source filter
- `bpipe/sample_aligner.c` - Multi-input synchronization
- `bpipe/core_filter.c` - Basic transform filter