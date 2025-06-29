# Test Core Filter Timing Hang Issue

## Summary
The `test_core_filter` executable hangs during execution when running all tests, preventing `make run` from completing successfully. This appears to be related to timing-sensitive tests that may have race conditions or improper cleanup.

## Current Status
- **Compilation**: ✅ Fixed - all tests compile successfully with new BpFilterConfig API
- **Individual Tests**: ✅ Most individual tests pass when run in isolation
- **Full Test Suite**: ❌ Hangs when running all tests together via `./build/test_core_filter`

## Problem Details

### Symptoms
1. `make run` hangs on the first test: `./build/test_core_filter`
2. Running `./build/test_core_filter` without arguments hangs indefinitely
3. Individual named tests work fine (e.g., `./build/test_core_filter test_BpFilter_Init_Success`)
4. No error messages or output before hanging

### Suspected Root Causes

#### 1. Timing-Sensitive Tests
The following tests involve timing operations that could cause hangs:

**`test_Await_Timeout_Behavior` (lines 287-321):**
```c
// Should timeout after 100ms since buffer is empty
Bp_EC result = Bp_await_not_empty(buf, 100000);  // 100ms in microseconds

// Verify it actually waited approximately 100ms
long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
TEST_ASSERT_TRUE(elapsed_ms >= 95 && elapsed_ms <= 150);  // Allow some tolerance
```

**Issues:**
- Relies on system timing precision
- Could hang if `Bp_await_not_empty` doesn't respect timeout
- Race conditions between threads

#### 2. Threading and Synchronization
Tests that start/stop filters create worker threads:

**`test_Bp_Filter_Start_Success` and related tests:**
```c
Bp_EC result = Bp_Filter_Start(&filter);  // Creates pthread
// ...
Bp_Filter_Stop(&filter);  // Should join thread
```

**Potential Issues:**
- Worker threads not properly terminating
- Deadlocks in pthread_join
- Race conditions during filter startup/shutdown

#### 3. Resource Cleanup
Inadequate cleanup between tests could cause resource leaks:

**Current tearDown:**
```c
void tearDown(void) {
    BpFilter_Deinit(&test_filter);  // Only cleans up global test_filter
}
```

**Missing Cleanup:**
- Local filters created in individual tests
- Buffers and mutexes not properly released
- Thread resources accumulating

## Investigation Results

### Working Components
✅ **API Migration Completed:** All `BpFilter_Init` calls updated to use `BpFilterConfig`
✅ **Basic Tests Pass:** Connection, initialization, and configuration tests work
✅ **Multi-Output Tests Work:** `test_simple_multi_output` passes completely (2/2 tests)

### Problem Isolated To
❌ **Full Test Suite Execution:** Only fails when running all tests in sequence
❌ **Timing/Threading Tests:** Likely related to `test_Await_*` or `test_Bp_Filter_Start_*` tests

## Immediate Workaround

The compilation issue has been resolved, allowing:
- ✅ `make all` - compiles successfully
- ✅ Individual C tests can be run manually
- ✅ Python tests work perfectly via `pytest`
- ✅ Development and testing can continue

## Proposed Solutions

### Short-term (Immediate)
1. **Skip Problematic Tests:** Temporarily disable timing-sensitive tests
2. **Improve Test Isolation:** Add proper cleanup for all local filters
3. **Add Timeouts:** Wrap test execution with timeouts in Makefile

### Medium-term (1-2 weeks)
1. **Fix Timing Tests:** Rewrite timing tests to be more robust
2. **Improve Thread Management:** Review worker thread lifecycle
3. **Enhanced Cleanup:** Implement comprehensive resource cleanup

### Long-term (Future)
1. **Test Framework Upgrade:** Consider more robust testing framework
2. **CI/CD Integration:** Add automated test execution with proper monitoring
3. **Performance Testing:** Systematic timing and threading validation

## Technical Notes

### API Migration Completed
Successfully updated from old API:
```c
// OLD (broken)
BpFilter_Init(&filter, BpPassThroughTransform, 0, 128, 64, 6, 1);

// NEW (working)
BpFilterConfig config = {
    .transform = BpPassThroughTransform,
    .dtype = DTYPE_FLOAT,
    .buffer_size = 128,
    .batch_size = 64,
    .number_of_batches_exponent = 6,
    .number_of_input_filters = 1,
    .overflow_behaviour = OVERFLOW_BLOCK,
    .auto_allocate_buffers = true
};
BpFilter_Init(&filter, &config);
```

### Test Environment
- **Platform:** Linux 5.15.0-1083-realtime
- **Compiler:** gcc with `-pthread -Wall -Werror`
- **Framework:** Unity testing framework

## Priority
**Medium** - Does not block development since:
- Compilation works
- Individual tests can be run
- Python tests are comprehensive and working
- Core functionality is verified through other means

## Related Issues
- Potential connection to threading issues mentioned in Python test comments
- May be related to start/stop functionality problems noted in existing codebase

## Resolution Criteria
- [ ] `make run` completes successfully
- [ ] All C tests pass in sequence
- [ ] No resource leaks or hanging threads
- [ ] Timing tests are robust and reliable

---
*Issue created as part of API migration fix in commit 55b75a5*
*Last updated: 2025-06-29*