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

To get more detailed failure information, tests should be instrumented:

```c
// In individual test files, add context before assertions:
void test_partial_batch_handling(void) {
    // ... setup ...
    
    // Add context before assertions that might fail
    if (output->head != expected_head) {
        // Log additional context before the assertion
        printf("DEBUG: Filter %s failed batch handling\n", g_filter_name);
        printf("  Expected head: %zu, Actual: %zu\n", expected_head, output->head);
        printf("  Batch capacity: %zu\n", batch_capacity);
    }
    
    TEST_ASSERT_EQUAL(expected_head, output->head);
}
```

Or use Unity's TEST_MESSAGE for context:

```c
TEST_MESSAGE("Testing partial batch with 32 samples in 64-capacity batch");
TEST_ASSERT_EQUAL_MESSAGE(32, output->head, 
                          "Partial batch should preserve sample count");
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

## Future Enhancements

### Phase 1 (Immediate)
- Basic CSV output with alignment
- Console output with formatting
- Pass/fail/skip tracking

### Phase 2 (Near-term)
- Regression detection via diff
- Detailed failure logging
- Summary statistics

### Phase 3 (Future)
- JSON output for programmatic access
- HTML report generation
- Historical trend tracking
- Performance metrics per test
- Test categories and filtering
- Watch mode for development

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