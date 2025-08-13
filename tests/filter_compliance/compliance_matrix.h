/**
 * @file compliance_matrix.h
 * @brief Test results matrix for filter compliance test suite
 * 
 * Phase 1 Implementation: Basic matrix with CSV output and console display
 */

#ifndef TEST_FILTER_COMPLIANCE_MATRIX_H
#define TEST_FILTER_COMPLIANCE_MATRIX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bperr.h"

// Test result states
typedef enum {
    TEST_RESULT_PASS,
    TEST_RESULT_FAIL,
    TEST_RESULT_SKIP,
    TEST_RESULT_NA,      // Not applicable
    TEST_RESULT_ERROR    // Test crashed or timed out
} TestResult_t;

// Test definition
typedef struct {
    char name[64];                    // Test name for column header
    char short_name[16];              // Abbreviated name if needed
} ComplianceTestDef_t;

// Row of test results for a single filter
typedef struct {
    char filter_name[64];             // Filter under test
    TestResult_t* results;            // Array of test results
    int pass_count;
    int fail_count;
    int skip_count;
    int error_count;
} FilterTestRow_t;

// Complete test matrix
typedef struct {
    ComplianceTestDef_t* tests;       // Array of all test definitions
    int n_tests;
    FilterTestRow_t* rows;            // Array of filter results
    int n_filters;
    int max_filters;                  // Allocated capacity
    char timestamp[32];               // ISO 8601 format
    char commit_hash[16];             // Git commit short hash
    char suite_version[16];           // Test suite version
} ComplianceMatrix_t;

// Core functions

/**
 * Initialize matrix with test definitions
 * @param matrix Matrix to initialize
 * @param max_filters Maximum number of filters to test
 * @return Bp_EC_OK on success
 */
Bp_EC compliance_matrix_init(ComplianceMatrix_t* matrix, int max_filters);

/**
 * Add a test definition to the matrix
 * @param matrix Matrix to add test to
 * @param test_name Full test name
 * @param short_name Abbreviated name for column headers
 * @return Bp_EC_OK on success
 */
Bp_EC compliance_matrix_add_test(ComplianceMatrix_t* matrix, 
                                 const char* test_name,
                                 const char* short_name);

/**
 * Start tracking results for a new filter
 * @param matrix Matrix to add filter to
 * @param filter_name Name of filter being tested
 * @return Index of the new filter row, or -1 on error
 */
int compliance_matrix_start_filter(ComplianceMatrix_t* matrix,
                                   const char* filter_name);

/**
 * Record a test result
 * @param matrix Matrix to update
 * @param filter_index Index of filter row
 * @param test_index Index of test
 * @param result Test result
 * @return Bp_EC_OK on success
 */
Bp_EC compliance_matrix_record_result(ComplianceMatrix_t* matrix,
                                      int filter_index,
                                      int test_index,
                                      TestResult_t result);

/**
 * Write matrix to CSV file
 * @param matrix Matrix to write
 * @param filename Output file path
 * @return Bp_EC_OK on success
 */
Bp_EC compliance_matrix_write_csv(const ComplianceMatrix_t* matrix,
                                  const char* filename);

/**
 * Print matrix to stdout with formatting
 * @param matrix Matrix to print
 * @param use_color Enable ANSI color codes
 * @return Bp_EC_OK on success
 */
Bp_EC compliance_matrix_print_stdout(const ComplianceMatrix_t* matrix,
                                     bool use_color);

/**
 * Clean up matrix resources
 * @param matrix Matrix to clean up
 */
void compliance_matrix_cleanup(ComplianceMatrix_t* matrix);

/**
 * Helper to convert Unity test result to TestResult_t
 * @param unity_failed Unity's CurrentTestFailed flag
 * @param unity_ignored Unity's CurrentTestIgnored flag
 * @return Corresponding TestResult_t value
 */
TestResult_t compliance_result_from_unity(int unity_failed, int unity_ignored);

/**
 * Get string representation of test result
 * @param result Test result
 * @return String representation (e.g., "PASS", "FAIL")
 */
const char* compliance_result_to_string(TestResult_t result);

#endif // TEST_FILTER_COMPLIANCE_MATRIX_H