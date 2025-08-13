# Filter Compliance Test Results Matrix Specification

## Overview

This specification defines a tabular test results reporting system for the bpipe2 filter compliance test suite. The system produces both human-readable aligned output and machine-parseable CSV files to track test results across all filters and enable easy regression detection.

## Motivation

With a growing number of filters and compliance tests, we need:
1. **Visual clarity** - Quickly identify which filters pass/fail which tests
2. **Regression tracking** - Detect when previously passing tests start failing
3. **Diff-friendly format** - Use standard diff tools to compare test runs
4. **CI/CD integration** - Machine-readable format for automated systems

## Output Format

### Primary Output: test_results.csv

Located in the repository root for easy access and version control visibility.

#### Format Requirements

1. **Fixed-width columns** - All columns padded for visual alignment
2. **Consistent ordering** - Filters sorted alphabetically, tests in fixed order
3. **Header row** - Test names as column headers
4. **Summary columns** - Pass/fail counts and percentages
5. **Metadata header** - Timestamp, commit hash, test suite version

#### Sample Output

```csv
# FILTER COMPLIANCE TEST RESULTS
# Generated: 2025-01-13 14:23:45 UTC
# Commit: 4f4686d
# Suite Version: 1.0.0
# Total Filters: 5
# Total Tests: 6
#
Filter              ,PropValid ,TypeCheck ,BatchSize ,Backpress ,Shutdown  ,Overflow  ,Pass ,Fail ,Skip ,Total    
signal_generator    ,PASS      ,PASS      ,PASS      ,PASS      ,PASS      ,SKIP      ,5    ,0    ,1    ,5/6      
csv_source          ,PASS      ,PASS      ,FAIL      ,PASS      ,PASS      ,PASS      ,5    ,1    ,0    ,5/6      
map_filter          ,PASS      ,PASS      ,PASS      ,PASS      ,PASS      ,PASS      ,6    ,0    ,0    ,6/6      
batch_matcher       ,PASS      ,SKIP      ,PASS      ,PASS      ,FAIL      ,PASS      ,4    ,1    ,1    ,4/6      
tee_filter          ,PASS      ,PASS      ,PASS      ,PASS      ,PASS      ,PASS      ,6    ,0    ,0    ,6/6      
TOTAL               ,5/5       ,4/5       ,4/5       ,5/5       ,4/5       ,4/5       ,26   ,2    ,2    ,26/30    
```

### Secondary Output: Console (stdout)

Human-optimized display with visual separators and optional color coding.

```
================================================================================
FILTER COMPLIANCE TEST RESULTS
Generated: 2025-01-13 14:23:45 UTC | Commit: 4f4686d | Suite: v1.0.0
================================================================================

Filter              | PropValid | TypeCheck | BatchSize | Backpress | Shutdown  | Overflow  | Score
--------------------|-----------|-----------|-----------|-----------|-----------|-----------|-------
signal_generator    | PASS      | PASS      | PASS      | PASS      | PASS      | SKIP      | 5/6
csv_source          | PASS      | PASS      | FAIL      | PASS      | PASS      | PASS      | 5/6  
map_filter          | PASS      | PASS      | PASS      | PASS      | PASS      | PASS      | 6/6
batch_matcher       | PASS      | SKIP      | PASS      | PASS      | FAIL      | PASS      | 4/6
tee_filter          | PASS      | PASS      | PASS      | PASS      | PASS      | PASS      | 6/6
--------------------|-----------|-----------|-----------|-----------|-----------|-----------|-------
SUMMARY             | 5/5       | 4/5       | 4/5       | 5/5       | 4/5       | 4/5       | 86.7%

Legend: PASS = Test passed | FAIL = Test failed | SKIP = Test skipped | N/A = Not applicable
```

## Result States

### Test Result Values

- **PASS** - Test executed successfully and all assertions passed
- **FAIL** - Test executed but one or more assertions failed
- **SKIP** - Test was intentionally skipped (not applicable to filter type)
- **N/A** - Test not applicable to this filter category
- **ERR** - Test encountered an error during execution (crash, timeout)

### Special Cases

- Source filters may skip sink-related tests
- Sink filters may skip source-related tests
- Multi-input filters have additional synchronization tests

## Implementation

### Data Structures

```c
typedef enum {
    TEST_RESULT_PASS,
    TEST_RESULT_FAIL,
    TEST_RESULT_SKIP,
    TEST_RESULT_NA,
    TEST_RESULT_ERROR
} TestResult_t;

typedef struct {
    char name[64];                    // Test name for column header
    char short_name[16];              // Abbreviated name if needed
    int (*test_fn)(Filter_t* filter); // Test function pointer
} ComplianceTest_t;

typedef struct {
    char filter_name[64];             // Filter under test
    TestResult_t* results;            // Array of test results
    int pass_count;
    int fail_count;
    int skip_count;
    int error_count;
    double avg_time_ms;               // Average test execution time
} FilterTestRow_t;

typedef struct {
    ComplianceTest_t* tests;         // Array of all tests
    int n_tests;
    FilterTestRow_t* rows;           // Array of filter results
    int n_filters;
    char timestamp[32];               // ISO 8601 format
    char commit_hash[16];            // Git commit short hash
    char suite_version[16];          // Test suite version
} ComplianceMatrix_t;
```

### Core Functions

```c
// Initialize matrix with test definitions
Bp_EC compliance_matrix_init(ComplianceMatrix_t* matrix);

// Run tests for a specific filter
Bp_EC compliance_run_filter_tests(ComplianceMatrix_t* matrix, 
                                  Filter_t* filter, 
                                  const char* filter_name);

// Output formatting functions
Bp_EC compliance_write_csv(const ComplianceMatrix_t* matrix, 
                           const char* filename);
Bp_EC compliance_print_stdout(const ComplianceMatrix_t* matrix, 
                              bool use_color);

// Calculate column widths for alignment
void compliance_calc_column_widths(const ComplianceMatrix_t* matrix,
                                   int* filter_width,
                                   int* test_widths);

// Comparison with previous results
Bp_EC compliance_diff_results(const char* current_file,
                              const char* previous_file,
                              char* diff_report,
                              size_t report_size);
```

### Test Discovery and Registration

The system builds on the existing test infrastructure in `tests/filter_compliance/main.c`:

#### Existing Test Structure

```c
// Already defined in main.c
typedef struct {
    void (*test_func)(void);
    const char* test_name;
    const char* test_file;
} ComplianceTest_t;

// Current test registry (to be extended with result tracking)
static ComplianceTest_t compliance_tests[] = {
    // Lifecycle tests
    {test_lifecycle_basic, "test_lifecycle_basic", ...},
    {test_lifecycle_with_worker, "test_lifecycle_with_worker", ...},
    {test_lifecycle_restart, "test_lifecycle_restart", ...},
    {test_lifecycle_errors, "test_lifecycle_errors", ...},
    
    // Connection tests
    {test_connection_single_sink, "test_connection_single_sink", ...},
    {test_connection_multi_sink, "test_connection_multi_sink", ...},
    {test_connection_type_safety, "test_connection_type_safety", ...},
    
    // Data flow tests
    {test_dataflow_passthrough, "test_dataflow_passthrough", ...},
    {test_dataflow_backpressure, "test_dataflow_backpressure", ...},
    
    // Error handling tests
    {test_error_invalid_config, "test_error_invalid_config", ...},
    {test_error_timeout, "test_error_timeout", ...},
    
    // Threading tests
    {test_thread_worker_lifecycle, "test_thread_worker_lifecycle", ...},
    {test_thread_shutdown_sync, "test_thread_shutdown_sync", ...},
    
    // Performance tests
    {test_perf_throughput, "test_perf_throughput", ...},
    
    // Buffer configuration tests
    {test_buffer_minimum_size, "test_buffer_minimum_size", ...},
    {test_buffer_overflow_drop_head, "test_buffer_overflow_drop_head", ...},
    {test_buffer_overflow_drop_tail, "test_buffer_overflow_drop_tail", ...},
    {test_buffer_large_batches, "test_buffer_large_batches", ...},
    
    // Behavioral compliance tests
    {test_partial_batch_handling, "test_partial_batch_handling", ...},
    {test_data_type_compatibility, "test_data_type_compatibility", ...},
    {test_sample_rate_preservation, "test_sample_rate_preservation", ...},
    {test_data_integrity, "test_data_integrity", ...},
    {test_multi_input_synchronization, "test_multi_input_synchronization", ...},
};
```

#### Filter Registration (from common.h)

```c
// Existing filter registration structure
typedef struct {
    const char* name;
    size_t filter_size;
    FilterInitFunc init;
    void* default_config;
    size_t config_size;
    size_t buff_config_offset;
    bool has_buff_config;
} FilterRegistration_t;

// Filters are registered in main.c
FilterRegistration_t filters[] = {
    {.name = "ControllableProducer", ...},
    {.name = "ControllableConsumer", ...},
    {.name = "Passthrough", ...},
    // Real filters to be added here
};
```

#### Integration with Unity Test Framework

The existing system uses Unity's test runner. We'll extend it to capture results:

```c
// Extension to capture results during test run
typedef struct {
    TestResult_t results[MAX_TESTS];
    int test_index;
} FilterTestCapture_t;

// Hook into Unity's test completion
void capture_unity_result(FilterTestCapture_t* capture, 
                          const char* test_name,
                          int unity_result) {
    if (unity_result == 0) {
        capture->results[capture->test_index] = TEST_RESULT_PASS;
    } else if (unity_result == UNITY_TEST_IGNORE) {
        capture->results[capture->test_index] = TEST_RESULT_SKIP;
    } else {
        capture->results[capture->test_index] = TEST_RESULT_FAIL;
    }
    capture->test_index++;
}
```

## Column Width Calculation

To ensure visual alignment while minimizing wasted space:

```c
void compliance_calc_column_widths(const ComplianceMatrix_t* matrix,
                                   int* filter_width,
                                   int* test_widths) {
    // Filter column: max filter name length + padding
    *filter_width = 20; // Minimum width
    for (int i = 0; i < matrix->n_filters; i++) {
        int len = strlen(matrix->rows[i].filter_name);
        if (len + 2 > *filter_width) {
            *filter_width = len + 2;
        }
    }
    
    // Test columns: max of test name and result string ("PASS", "FAIL", etc.)
    for (int i = 0; i < matrix->n_tests; i++) {
        int min_width = 10; // Minimum for readability
        int name_len = strlen(matrix->tests[i].name);
        test_widths[i] = (name_len > min_width) ? name_len + 1 : min_width;
    }
}
```

## File Management

### Output Files

- `test_results.csv` - Current test results (overwritten each run)
- `test_results_prev.csv` - Previous results (copied before new run)
- `test_failures.log` - Detailed failure information
- `test_results_<timestamp>.csv` - Archived results (optional)

### Failure Details Log

Unity provides limited failure information through its global state structure. What we can realistically capture:

#### Available from Unity Framework

```c
// Unity provides these in its global struct:
Unity.TestFile          // File where test is defined
Unity.CurrentTestName   // Name of the failing test  
Unity.CurrentTestLineNumber // Line where assertion failed
Unity.CurrentTestFailed // Boolean flag
Unity.CurrentDetail1/2  // Optional detail strings (if not excluded)
```

#### Realistic Failure Log Format

```
================================================================================
[2025-01-13 14:23:45] csv_source::test_partial_batch_handling
--------------------------------------------------------------------------------
Test File: tests/filter_compliance/test_behavioral_compliance.c
Failed At: Line 234
Filter: csv_source
Test: test_partial_batch_handling

Unity Output:
  test_behavioral_compliance.c:234:FAIL: Expected 32 Was 0

Note: For more detailed failure information, consider adding custom logging
      within individual test functions using TEST_MESSAGE() or printf().
================================================================================
```

#### Enhanced Error Capture Strategy

##### Stdout/Stderr Capture Per Test

We can capture all output (Unity messages, printf debug, TEST_MESSAGE) for each test by redirecting stdout before each test run:

```c
// Test output capture structure
typedef struct {
    int original_stdout;
    int original_stderr;
    int capture_pipe[2];
    char* captured_output;
    size_t output_size;
    size_t output_capacity;
} TestOutputCapture_t;

// Initialize capture before each test
void test_capture_begin(TestOutputCapture_t* capture) {
    capture->output_size = 0;
    
    // Create pipe for capturing
    pipe(capture->capture_pipe);
    
    // Save original stdout/stderr
    capture->original_stdout = dup(STDOUT_FILENO);
    capture->original_stderr = dup(STDERR_FILENO);
    
    // Redirect stdout/stderr to pipe
    dup2(capture->capture_pipe[1], STDOUT_FILENO);
    dup2(capture->capture_pipe[1], STDERR_FILENO);
    close(capture->capture_pipe[1]);
    
    // Set non-blocking read
    fcntl(capture->capture_pipe[0], F_SETFL, O_NONBLOCK);
}

// Read captured output after test
void test_capture_end(TestOutputCapture_t* capture) {
    // Restore original stdout/stderr
    fflush(stdout);
    fflush(stderr);
    dup2(capture->original_stdout, STDOUT_FILENO);
    dup2(capture->original_stderr, STDERR_FILENO);
    
    // Read from pipe
    char buffer[4096];
    ssize_t bytes;
    while ((bytes = read(capture->capture_pipe[0], buffer, sizeof(buffer))) > 0) {
        // Append to capture->captured_output
        test_capture_append(capture, buffer, bytes);
    }
    
    close(capture->capture_pipe[0]);
    close(capture->original_stdout);
    close(capture->original_stderr);
}
```

##### Integration with Test Runner

```c
// Modified test loop in main.c
TestOutputCapture_t capture;
test_capture_init(&capture);

for (size_t i = 0; i < test_matrix.n_tests; i++) {
    // Start capturing output for this test
    test_capture_begin(&capture);
    
    // Run the test (all TEST_MESSAGE, printf, Unity output captured)
    int unity_result = UnityDefaultTestRun(...);
    
    // Stop capturing and collect output
    test_capture_end(&capture);
    
    // Store result
    if (Unity.CurrentTestFailed) {
        row->results[i] = TEST_RESULT_FAIL;
        
        // Save captured output for failed tests
        save_test_failure_details(filter_name, test_name, 
                                 capture.captured_output);
    }
    
    test_capture_reset(&capture);
}
```

##### Rich Failure Output

With capture in place, tests can be instrumented freely:

```c
void test_partial_batch_handling(void) {
    TEST_MESSAGE("=== Testing Partial Batch Handling ===");
    TEST_MESSAGE("Filter under test: " g_filter_name);
    TEST_MESSAGE("Configuration: 32 samples in 64-capacity batch");
    
    // Setup...
    Batch_t* input = create_partial_batch(32, 64);
    printf("Input batch created: head=%zu, capacity=%zu\n", 
           input->head, batch_capacity);
    
    // Process...
    Bp_EC result = process_batch(filter, input, output);
    printf("Processing result: %s (code=%d)\n", 
           bp_error_str(result), result);
    
    // Detailed diagnostics before assertion
    printf("Output state: head=%zu, t_ns=%llu, period_ns=%llu\n",
           output->head, output->t_ns, output->period_ns);
    
    // This assertion might fail
    TEST_ASSERT_EQUAL_MESSAGE(32, output->head,
                              "Output should preserve input sample count");
}
```

##### Resulting Failure Log

```
================================================================================
[2025-01-13 14:23:45] csv_source::test_partial_batch_handling FAILED
--------------------------------------------------------------------------------
Test File: tests/filter_compliance/test_behavioral_compliance.c:234

Captured Test Output:
=== Testing Partial Batch Handling ===
Filter under test: csv_source
Configuration: 32 samples in 64-capacity batch
Input batch created: head=32, capacity=64
Processing result: OK (code=0)
Output state: head=0, t_ns=1000000, period_ns=20833
test_behavioral_compliance.c:234:FAIL: Expected 32 Was 0
  Message: Output should preserve input sample count

Analysis: The filter appears to have lost samples during processing.
         Input had 32 samples but output has 0.
================================================================================
```

##### Alternative: Unity Custom Output

Instead of pipe redirection, we could also define a custom Unity output handler:

```c
// Buffer for capturing Unity output
static char g_unity_capture[8192];
static size_t g_unity_capture_pos;

// Custom output function
void unity_output_capture(int c) {
    if (g_unity_capture_pos < sizeof(g_unity_capture) - 1) {
        g_unity_capture[g_unity_capture_pos++] = c;
    }
    // Also output to console
    putchar(c);
}

// Before running tests
#define UNITY_OUTPUT_CHAR(c) unity_output_capture(c)

// Reset before each test
g_unity_capture_pos = 0;
memset(g_unity_capture, 0, sizeof(g_unity_capture));
```

## Usage Integration

### Modified Test Runner

The existing `main.c` test runner needs minimal modifications to output the matrix:

```c
// In main.c, add matrix tracking
ComplianceMatrix_t test_matrix;

int main(int argc, char* argv[]) {
    // Existing command line parsing...
    
    // Initialize matrix
    compliance_matrix_init(&test_matrix);
    test_matrix.n_tests = sizeof(compliance_tests) / sizeof(compliance_tests[0]);
    
    // Copy test names
    for (size_t i = 0; i < test_matrix.n_tests; i++) {
        strncpy(test_matrix.tests[i].name, 
                compliance_tests[i].test_name, 63);
    }
    
    // Existing filter loop
    for (g_current_filter = 0; g_current_filter < g_n_filters; g_current_filter++) {
        // ... existing filter pattern matching ...
        
        // Create row for this filter
        FilterTestRow_t* row = &test_matrix.rows[test_matrix.n_filters];
        strncpy(row->filter_name, filters[g_current_filter].name, 63);
        row->results = calloc(test_matrix.n_tests, sizeof(TestResult_t));
        
        UNITY_BEGIN();
        
        for (size_t i = 0; i < test_matrix.n_tests; i++) {
            // ... existing test pattern matching ...
            
            // Capture Unity result
            int unity_result = UnityDefaultTestRun(...);
            
            // Store in matrix
            if (Unity.CurrentTestIgnored) {
                row->results[i] = TEST_RESULT_SKIP;
                row->skip_count++;
            } else if (Unity.CurrentTestFailed) {
                row->results[i] = TEST_RESULT_FAIL;
                row->fail_count++;
            } else {
                row->results[i] = TEST_RESULT_PASS;
                row->pass_count++;
            }
        }
        
        UNITY_END();
        test_matrix.n_filters++;
    }
    
    // Output matrix
    compliance_write_csv(&test_matrix, "test_results.csv");
    compliance_print_stdout(&test_matrix, isatty(STDOUT_FILENO));
    
    // Cleanup
    for (int i = 0; i < test_matrix.n_filters; i++) {
        free(test_matrix.rows[i].results);
    }
    
    return Unity.TestFailures > 0 ? 1 : 0;
}
```

### Makefile Target

```makefile
test-compliance: build
	@echo "Running filter compliance test suite..."
	@if [ -f test_results.csv ]; then cp test_results.csv test_results_prev.csv; fi
	@./build/test_filter_compliance
	@echo "Results written to test_results.csv"
	@if grep -q "FAIL\|ERR" test_results.csv; then \
		echo "⚠️  Some tests failed. See test_failures.log for details"; \
		exit 1; \
	else \
		echo "✅ All tests passed!"; \
	fi

test-compliance-diff: test-compliance
	@if [ -f test_results_prev.csv ]; then \
		./scripts/diff_test_results.sh test_results_prev.csv test_results.csv; \
	else \
		echo "No previous results to compare"; \
	fi

# Run tests for specific filter
test-compliance-filter: build
	@./build/test_filter_compliance --filter $(FILTER)

# Run specific test across all filters  
test-compliance-test: build
	@./build/test_filter_compliance --test $(TEST)
```

### CI/CD Integration

```yaml
# .github/workflows/compliance.yml
- name: Run Compliance Tests
  run: make test-compliance
  
- name: Upload Test Results
  if: always()
  uses: actions/upload-artifact@v2
  with:
    name: compliance-test-results
    path: |
      test_results.csv
      test_failures.log
      
- name: Comment PR with Results
  if: github.event_name == 'pull_request'
  run: |
    python scripts/format_test_results.py test_results.csv > results.md
    gh pr comment ${{ github.event.pull_request.number }} --body-file results.md
```

## Phased Implementation Plan

### Phase 1: Basic Matrix (MVP)
**Goal**: Demonstrate the core concept with minimal changes

**Deliverables**:
1. Basic CSV output to `test_results.csv`
2. Simple console matrix display
3. Pass/fail/skip counting
4. Integration with existing Unity test runner

**Implementation**:
```c
// Minimal changes to main.c
ComplianceMatrix_t matrix;
// Track results during existing test loop
// Output at end of run
```

**Success Criteria**:
- Generates readable CSV file
- Shows all filters × all tests
- Preserves existing test functionality
- Can be diffed between runs

---

### Phase 2: Visual Improvements
**Goal**: Make the matrix production-ready for daily use

**Deliverables**:
1. Column width optimization for readability
2. Colored console output (when terminal supports)
3. Summary row/column with totals
4. Abbreviated column headers for wide matrices
5. Automatic backup of previous results

**Implementation**:
- Dynamic column width calculation
- ANSI color codes for pass/fail/skip
- Smart abbreviation system

**Success Criteria**:
- Matrix fits in standard terminal width
- Visual scanning is easy
- Regressions immediately visible

---

### Phase 3: Basic Failure Logging
**Goal**: Capture Unity's failure messages

**Deliverables**:
1. Capture Unity failure output (line number, expected/actual)
2. Write failures to `test_failures.log`
3. Link matrix results to failure details
4. Add timestamp and commit info

**Implementation**:
```c
// Capture Unity.CurrentTestFailed state
// Log Unity.TestFile:CurrentTestLineNumber
// Save failure message from Unity output
```

**Success Criteria**:
- Failed tests have accessible details
- No loss of existing Unity output
- Failure log is human-readable

---

### Phase 4: Enhanced Output Capture
**Goal**: Rich debugging information for failures

**Deliverables**:
1. Per-test stdout/stderr capture system
2. TEST_MESSAGE() and printf() capture
3. Selective storage (only failed tests)
4. Buffer management (8KB per test limit)

**Implementation**:
```c
// Pipe redirection approach
TestOutputCapture_t capture;
test_capture_begin(&capture);
// Run test
test_capture_end(&capture);
if (failed) save_output(capture);
```

**Success Criteria**:
- All test output captured
- Failed tests have full context
- No impact on passing test performance
- Works with existing TEST_MESSAGE() calls

---

### Phase 5: Advanced Features
**Goal**: Enterprise-ready test reporting

**Deliverables**:
1. JSON output for CI/CD integration
2. HTML report with drill-down
3. Historical trend tracking
4. Performance metrics (test duration)
5. Regression detection and highlighting
6. Test categorization and filtering

**Implementation**:
- Multiple output formats
- SQLite for historical data
- JavaScript for interactive HTML

**Success Criteria**:
- CI/CD systems can parse results
- Trends visible over time
- Performance regressions detected

---

### Phase 6: Developer Experience
**Goal**: Optimize for development workflow

**Deliverables**:
1. Watch mode (re-run on file change)
2. Focused testing (run single filter/test)
3. Baseline management (accept new results)
4. Diff visualization tool
5. IDE integration helpers

**Implementation**:
- File system watcher
- Interactive diff viewer
- VS Code extension for result viewing

**Success Criteria**:
- Rapid test-fix-verify cycle
- Easy regression investigation
- Integrated into developer workflow

## Implementation Timeline

| Phase | Effort | Priority | Dependencies |
|-------|--------|----------|--------------|
| 1. Basic Matrix | 2-3 hours | **HIGH** | None |
| 2. Visual Improvements | 2-3 hours | **HIGH** | Phase 1 |
| 3. Basic Failure Logging | 1-2 hours | **MEDIUM** | Phase 1 |
| 4. Output Capture | 4-6 hours | **MEDIUM** | Phase 3 |
| 5. Advanced Features | 8-12 hours | **LOW** | Phase 4 |
| 6. Developer Experience | 6-8 hours | **LOW** | Phase 5 |

## Rollout Strategy

1. **Phase 1-2**: Ship immediately for team feedback
2. **Phase 3**: Add based on initial usage patterns
3. **Phase 4**: Implement when debugging needs arise
4. **Phase 5-6**: Based on team size and CI/CD requirements

## Risk Mitigation

- **Phase 1**: Keep changes minimal, don't break existing tests
- **Phase 2**: Make visual features optional (--no-color flag)
- **Phase 3**: Append-only logging to avoid data loss
- **Phase 4**: Add capture as opt-in (--capture flag initially)
- **Phase 5**: Keep JSON schema versioned for compatibility
- **Phase 6**: All features optional, core functionality unchanged

## Example Test Implementation

```c
// Example compliance test
int test_batch_size_handling(Filter_t* filter) {
    // Setup
    TestHarness_t harness;
    test_harness_init(&harness, filter);
    
    // Test partial batch handling
    Batch_t* input = create_partial_batch(32, 64); // 32 samples in 64-capacity
    Bp_EC result = test_harness_process(&harness, input);
    
    if (result != Bp_EC_OK) {
        test_log_failure("Failed to process partial batch: %s", 
                        bp_error_str(result));
        return TEST_RESULT_FAIL;
    }
    
    // Verify output
    Batch_t* output = test_harness_get_output(&harness);
    if (output->head != 32) {
        test_log_failure("Incorrect output size. Expected: 32, Got: %zu", 
                        output->head);
        return TEST_RESULT_FAIL;
    }
    
    test_harness_cleanup(&harness);
    return TEST_RESULT_PASS;
}
```

## Success Criteria

1. **Visual Clarity** - Results are immediately understandable at a glance
2. **Diff-friendly** - Format remains stable across runs for easy comparison
3. **Comprehensive** - All filters and tests are represented
4. **Actionable** - Failures link to detailed diagnostic information
5. **Performant** - Test suite completes in reasonable time (<30 seconds)
6. **Maintainable** - Easy to add new filters and tests

## Appendix: Column Abbreviations

### Current Test Categories and Abbreviations

Based on the existing compliance tests in `main.c`:

| Test Category | Tests | Short Name | Column Header |
|---------------|-------|------------|---------------|
| **Lifecycle** | | | |
| | test_lifecycle_basic | Basic | LifeBasic |
| | test_lifecycle_with_worker | Worker | LifeWork |
| | test_lifecycle_restart | Restart | LifeRest |
| | test_lifecycle_errors | Errors | LifeErr |
| **Connection** | | | |
| | test_connection_single_sink | Single | ConnSingle |
| | test_connection_multi_sink | Multi | ConnMulti |
| | test_connection_type_safety | Type | ConnType |
| **Data Flow** | | | |
| | test_dataflow_passthrough | Pass | DataPass |
| | test_dataflow_backpressure | BkPr | DataBkPr |
| **Error Handling** | | | |
| | test_error_invalid_config | Config | ErrConfig |
| | test_error_timeout | Timeout | ErrTime |
| **Threading** | | | |
| | test_thread_worker_lifecycle | Worker | ThrdWork |
| | test_thread_shutdown_sync | Sync | ThrdSync |
| **Performance** | | | |
| | test_perf_throughput | Thruput | PerfThru |
| **Buffer Edge Cases** | | | |
| | test_buffer_minimum_size | MinSize | BuffMin |
| | test_buffer_overflow_drop_head | DropHd | BuffDrpH |
| | test_buffer_overflow_drop_tail | DropTl | BuffDrpT |
| | test_buffer_large_batches | Large | BuffLarge |
| **Behavioral Compliance** | | | |
| | test_partial_batch_handling | Partial | BehvPart |
| | test_data_type_compatibility | Type | BehvType |
| | test_sample_rate_preservation | Rate | BehvRate |
| | test_data_integrity | Integrity | BehvIntg |
| | test_multi_input_synchronization | Sync | BehvSync |

### Compact Matrix Example

When using abbreviated column headers for wide matrices:

```
Filter          |LfBsc|LfWrk|LfRst|LfErr|CnSng|CnMlt|CnTyp|DtPas|DtBkP|ErCfg|ErTim|TdWrk|TdSnc|PfThr|BfMin|BfDrH|BfDrT|BfLrg|BhPrt|BhTyp|BhRat|BhInt|BhSnc|Total
----------------|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----
signal_generator|PASS |PASS |PASS |PASS |SKIP |SKIP |SKIP |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |N/A  |19/23
passthrough     |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |PASS |SKIP |22/23
```

### Helper Macros in Tests

The existing `common.h` provides skip macros that map to our result states:

- `SKIP_IF_NO_INPUTS()` → TEST_RESULT_SKIP for source filters on input tests
- `SKIP_IF_NO_OUTPUTS()` → TEST_RESULT_SKIP for sink filters on output tests  
- `SKIP_IF_NO_WORKER()` → TEST_RESULT_SKIP for filters without worker threads
- `TEST_IGNORE_MESSAGE()` → TEST_RESULT_SKIP with custom reason