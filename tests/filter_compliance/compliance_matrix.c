/**
 * @file compliance_matrix.c
 * @brief Implementation of test results matrix for filter compliance
 * 
 * Phase 1: Basic matrix with CSV output and console display
 */

#include "compliance_matrix.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// ANSI color codes
#define COLOR_RESET   "\x1b[0m"
#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_GRAY    "\x1b[90m"

Bp_EC compliance_matrix_init(ComplianceMatrix_t* matrix, int max_filters)
{
    if (!matrix || max_filters <= 0) {
        return Bp_EC_INVALID_CONFIG;
    }
    
    memset(matrix, 0, sizeof(ComplianceMatrix_t));
    
    // Allocate test definitions array (start with space for 32 tests)
    matrix->tests = calloc(32, sizeof(ComplianceTestDef_t));
    if (!matrix->tests) {
        return Bp_EC_ALLOC;
    }
    
    // Allocate filter rows array
    matrix->rows = calloc(max_filters, sizeof(FilterTestRow_t));
    if (!matrix->rows) {
        free(matrix->tests);
        return Bp_EC_ALLOC;
    }
    
    matrix->max_filters = max_filters;
    matrix->n_filters = 0;
    matrix->n_tests = 0;
    
    // Set timestamp
    time_t now = time(NULL);
    struct tm* tm_info = gmtime(&now);
    strftime(matrix->timestamp, sizeof(matrix->timestamp), 
             "%Y-%m-%d %H:%M:%S UTC", tm_info);
    
    // Get git commit hash (simplified for Phase 1)
    FILE* fp = popen("git rev-parse --short HEAD 2>/dev/null", "r");
    if (fp) {
        if (fgets(matrix->commit_hash, sizeof(matrix->commit_hash), fp)) {
            // Remove trailing newline
            matrix->commit_hash[strcspn(matrix->commit_hash, "\n")] = 0;
        }
        pclose(fp);
    }
    if (strlen(matrix->commit_hash) == 0) {
        strcpy(matrix->commit_hash, "unknown");
    }
    
    // Set suite version
    strcpy(matrix->suite_version, "1.0.0");
    
    return Bp_EC_OK;
}

Bp_EC compliance_matrix_add_test(ComplianceMatrix_t* matrix, 
                                 const char* test_name,
                                 const char* short_name)
{
    if (!matrix || !test_name) {
        return Bp_EC_INVALID_CONFIG;
    }
    
    // For Phase 1, assume we won't exceed 32 tests
    if (matrix->n_tests >= 32) {
        return Bp_EC_NO_SPACE;
    }
    
    ComplianceTestDef_t* test = &matrix->tests[matrix->n_tests];
    strncpy(test->name, test_name, sizeof(test->name) - 1);
    test->name[sizeof(test->name) - 1] = '\0';
    
    if (short_name) {
        strncpy(test->short_name, short_name, sizeof(test->short_name) - 1);
        test->short_name[sizeof(test->short_name) - 1] = '\0';
    } else {
        // Auto-generate short name from test name
        // Take first 10 chars or up to first underscore after "test_"
        const char* src = test_name;
        if (strncmp(src, "test_", 5) == 0) {
            src += 5;  // Skip "test_" prefix
        }
        
        // Copy up to 15 chars for short name
        int i;
        for (i = 0; i < 15 && src[i] != '\0'; i++) {
            test->short_name[i] = src[i];
        }
        test->short_name[i] = '\0';
    }
    
    matrix->n_tests++;
    return Bp_EC_OK;
}

int compliance_matrix_start_filter(ComplianceMatrix_t* matrix,
                                   const char* filter_name)
{
    if (!matrix || !filter_name) {
        return -1;
    }
    
    if (matrix->n_filters >= matrix->max_filters) {
        return -1;
    }
    
    FilterTestRow_t* row = &matrix->rows[matrix->n_filters];
    
    // Initialize row
    memset(row, 0, sizeof(FilterTestRow_t));
    strncpy(row->filter_name, filter_name, sizeof(row->filter_name) - 1);
    row->filter_name[sizeof(row->filter_name) - 1] = '\0';
    
    // Allocate results array
    row->results = calloc(matrix->n_tests, sizeof(TestResult_t));
    if (!row->results) {
        return -1;
    }
    
    // Initialize all results to NA
    for (int i = 0; i < matrix->n_tests; i++) {
        row->results[i] = TEST_RESULT_NA;
    }
    
    int index = matrix->n_filters;
    matrix->n_filters++;
    return index;
}

Bp_EC compliance_matrix_record_result(ComplianceMatrix_t* matrix,
                                      int filter_index,
                                      int test_index,
                                      TestResult_t result)
{
    if (!matrix || filter_index < 0 || filter_index >= matrix->n_filters ||
        test_index < 0 || test_index >= matrix->n_tests) {
        return Bp_EC_INVALID_CONFIG;
    }
    
    FilterTestRow_t* row = &matrix->rows[filter_index];
    
    // Only update counters if this is a new result (not overwriting)
    TestResult_t old_result = row->results[test_index];
    
    // Update result
    row->results[test_index] = result;
    
    // Decrement old counter if it was already set
    if (old_result != TEST_RESULT_NA) {
        switch (old_result) {
            case TEST_RESULT_PASS: row->pass_count--; break;
            case TEST_RESULT_FAIL: row->fail_count--; break;
            case TEST_RESULT_SKIP: row->skip_count--; break;
            case TEST_RESULT_ERROR: row->error_count--; break;
            default: break;
        }
    }
    
    // Update counters for new result
    switch (result) {
        case TEST_RESULT_PASS:
            row->pass_count++;
            break;
        case TEST_RESULT_FAIL:
            row->fail_count++;
            break;
        case TEST_RESULT_SKIP:
            row->skip_count++;
            break;
        case TEST_RESULT_ERROR:
            row->error_count++;
            break;
        case TEST_RESULT_NA:
            // Don't count N/A
            break;
    }
    
    return Bp_EC_OK;
}

Bp_EC compliance_matrix_write_csv(const ComplianceMatrix_t* matrix,
                                  const char* filename)
{
    if (!matrix || !filename) {
        return Bp_EC_INVALID_CONFIG;
    }
    
    FILE* fp = fopen(filename, "w");
    if (!fp) {
        return Bp_EC_INVALID_DATA;  // Using this as a file error placeholder
    }
    
    // Write metadata header
    fprintf(fp, "# FILTER COMPLIANCE TEST RESULTS\n");
    fprintf(fp, "# Generated: %s\n", matrix->timestamp);
    fprintf(fp, "# Commit: %s\n", matrix->commit_hash);
    fprintf(fp, "# Suite Version: %s\n", matrix->suite_version);
    fprintf(fp, "# Total Filters: %d\n", matrix->n_filters);
    fprintf(fp, "# Total Tests: %d\n", matrix->n_tests);
    fprintf(fp, "#\n");
    
    // Calculate column widths for alignment
    int filter_col_width = 20;  // Minimum width for filter column
    for (int i = 0; i < matrix->n_filters; i++) {
        int len = strlen(matrix->rows[i].filter_name);
        if (len > filter_col_width) {
            filter_col_width = len;
        }
    }
    
    // Write header row
    fprintf(fp, "%-*s", filter_col_width, "Filter");
    for (int i = 0; i < matrix->n_tests; i++) {
        fprintf(fp, ",%-10s", matrix->tests[i].short_name);
    }
    fprintf(fp, ",%-5s,%-5s,%-5s,%-8s\n", "Pass", "Fail", "Skip", "Total");
    
    // Write filter rows
    for (int i = 0; i < matrix->n_filters; i++) {
        FilterTestRow_t* row = &matrix->rows[i];
        
        fprintf(fp, "%-*s", filter_col_width, row->filter_name);
        
        for (int j = 0; j < matrix->n_tests; j++) {
            fprintf(fp, ",%-10s", compliance_result_to_string(row->results[j]));
        }
        
        // Summary columns
        fprintf(fp, ",%-5d,%-5d,%-5d,%d/%d\n",
                row->pass_count, row->fail_count, row->skip_count,
                row->pass_count, matrix->n_tests);
    }
    
    // Write summary row
    fprintf(fp, "%-*s", filter_col_width, "TOTAL");
    
    // Calculate totals for each test
    for (int j = 0; j < matrix->n_tests; j++) {
        int pass = 0, fail = 0, skip = 0;
        for (int i = 0; i < matrix->n_filters; i++) {
            switch (matrix->rows[i].results[j]) {
                case TEST_RESULT_PASS: pass++; break;
                case TEST_RESULT_FAIL: fail++; break;
                case TEST_RESULT_SKIP: skip++; break;
                default: break;
            }
        }
        fprintf(fp, ",%d/%d      ", pass, matrix->n_filters);
    }
    
    // Grand totals
    int total_pass = 0, total_fail = 0, total_skip = 0;
    for (int i = 0; i < matrix->n_filters; i++) {
        total_pass += matrix->rows[i].pass_count;
        total_fail += matrix->rows[i].fail_count;
        total_skip += matrix->rows[i].skip_count;
    }
    int total_tests = matrix->n_filters * matrix->n_tests;
    fprintf(fp, ",%-5d,%-5d,%-5d,%d/%d\n",
            total_pass, total_fail, total_skip,
            total_pass, total_tests);
    
    fclose(fp);
    return Bp_EC_OK;
}

Bp_EC compliance_matrix_print_stdout(const ComplianceMatrix_t* matrix,
                                     bool use_color)
{
    if (!matrix) {
        return Bp_EC_INVALID_CONFIG;
    }
    
    // Header
    printf("\n");
    printf("================================================================================\n");
    printf("FILTER COMPLIANCE TEST RESULTS\n");
    printf("Generated: %s | Commit: %s | Suite: v%s\n",
           matrix->timestamp, matrix->commit_hash, matrix->suite_version);
    printf("================================================================================\n\n");
    
    // Calculate column widths
    int filter_col_width = 20;
    for (int i = 0; i < matrix->n_filters; i++) {
        int len = strlen(matrix->rows[i].filter_name);
        if (len > filter_col_width) {
            filter_col_width = len;
        }
    }
    
    // Table header
    printf("%-*s |", filter_col_width, "Filter");
    for (int i = 0; i < matrix->n_tests; i++) {
        // Use short names if they fit, otherwise truncate
        char header[11];
        strncpy(header, matrix->tests[i].short_name, 10);
        header[10] = '\0';
        printf(" %-10s|", header);
    }
    printf(" Score\n");
    
    // Separator line
    for (int i = 0; i < filter_col_width; i++) printf("-");
    printf("-|");
    for (int i = 0; i < matrix->n_tests; i++) {
        printf("-----------|");
    }
    printf("-------\n");
    
    // Filter rows
    for (int i = 0; i < matrix->n_filters; i++) {
        FilterTestRow_t* row = &matrix->rows[i];
        
        printf("%-*s |", filter_col_width, row->filter_name);
        
        for (int j = 0; j < matrix->n_tests; j++) {
            const char* result_str = compliance_result_to_string(row->results[j]);
            
            if (use_color) {
                switch (row->results[j]) {
                    case TEST_RESULT_PASS:
                        printf(" %s%-10s%s|", COLOR_GREEN, result_str, COLOR_RESET);
                        break;
                    case TEST_RESULT_FAIL:
                        printf(" %s%-10s%s|", COLOR_RED, result_str, COLOR_RESET);
                        break;
                    case TEST_RESULT_SKIP:
                        printf(" %s%-10s%s|", COLOR_YELLOW, result_str, COLOR_RESET);
                        break;
                    default:
                        printf(" %s%-10s%s|", COLOR_GRAY, result_str, COLOR_RESET);
                        break;
                }
            } else {
                printf(" %-10s|", result_str);
            }
        }
        
        // Score
        printf(" %d/%d\n", row->pass_count, matrix->n_tests);
    }
    
    // Summary separator
    for (int i = 0; i < filter_col_width; i++) printf("-");
    printf("-|");
    for (int i = 0; i < matrix->n_tests; i++) {
        printf("-----------|");
    }
    printf("-------\n");
    
    // Summary row
    printf("%-*s |", filter_col_width, "SUMMARY");
    
    // Calculate totals for each test
    int total_pass = 0, total_fail = 0, total_skip = 0;
    for (int j = 0; j < matrix->n_tests; j++) {
        int pass = 0, fail = 0, skip = 0;
        for (int i = 0; i < matrix->n_filters; i++) {
            switch (matrix->rows[i].results[j]) {
                case TEST_RESULT_PASS: pass++; break;
                case TEST_RESULT_FAIL: fail++; break;
                case TEST_RESULT_SKIP: skip++; break;
                default: break;
            }
        }
        printf(" %d/%-8d|", pass, matrix->n_filters);
        total_pass += pass;
        total_fail += fail;
        total_skip += skip;
    }
    
    // Overall percentage
    int total_tests = matrix->n_filters * matrix->n_tests;
    if (total_tests > 0) {
        printf(" %.1f%%\n", (100.0 * total_pass) / total_tests);
    } else {
        printf(" N/A\n");
    }
    
    printf("\nLegend: PASS = Test passed | FAIL = Test failed | SKIP = Test skipped | N/A = Not applicable\n");
    
    return Bp_EC_OK;
}

void compliance_matrix_cleanup(ComplianceMatrix_t* matrix)
{
    if (!matrix) {
        return;
    }
    
    // Free test definitions
    if (matrix->tests) {
        free(matrix->tests);
        matrix->tests = NULL;
    }
    
    // Free filter rows and their results
    if (matrix->rows) {
        for (int i = 0; i < matrix->n_filters; i++) {
            if (matrix->rows[i].results) {
                free(matrix->rows[i].results);
            }
        }
        free(matrix->rows);
        matrix->rows = NULL;
    }
    
    matrix->n_tests = 0;
    matrix->n_filters = 0;
    matrix->max_filters = 0;
}

TestResult_t compliance_result_from_unity(int unity_failed, int unity_ignored)
{
    if (unity_ignored) {
        return TEST_RESULT_SKIP;
    } else if (unity_failed) {
        return TEST_RESULT_FAIL;
    } else {
        return TEST_RESULT_PASS;
    }
}

const char* compliance_result_to_string(TestResult_t result)
{
    switch (result) {
        case TEST_RESULT_PASS:  return "PASS";
        case TEST_RESULT_FAIL:  return "FAIL";
        case TEST_RESULT_SKIP:  return "SKIP";
        case TEST_RESULT_NA:    return "N/A";
        case TEST_RESULT_ERROR: return "ERR";
        default:                return "?";
    }
}

// Helper function to determine test category from test name
static const char* get_test_category(const char* test_name)
{
    // Check threading tests first (to avoid confusion with lifecycle)
    if (strstr(test_name, "thread")) return "Threading";
    if (strstr(test_name, "lifecycle")) return "Lifecycle";
    if (strstr(test_name, "connection")) return "Connection";
    if (strstr(test_name, "dataflow")) return "Data Flow";
    if (strstr(test_name, "error")) return "Error Handling";
    if (strstr(test_name, "perf")) return "Performance";
    if (strstr(test_name, "buffer")) return "Buffer Config";
    if (strstr(test_name, "overflow")) return "Buffer Config";
    if (strstr(test_name, "partial") || strstr(test_name, "data_type") || 
        strstr(test_name, "sample_rate") || strstr(test_name, "data_integrity") ||
        strstr(test_name, "multi_input")) return "Behavioral";
    return "Other";
}

Bp_EC compliance_matrix_print_grouped(const ComplianceMatrix_t* matrix,
                                      bool use_color)
{
    if (!matrix) {
        return Bp_EC_INVALID_CONFIG;
    }
    
    // Header
    printf("\n");
    printf("================================================================================\n");
    printf("FILTER COMPLIANCE TEST RESULTS\n");
    printf("Generated: %s | Commit: %s | Suite: v%s\n",
           matrix->timestamp, matrix->commit_hash, matrix->suite_version);
    printf("================================================================================\n\n");
    
    // Calculate column widths
    int filter_col_width = 20;
    for (int i = 0; i < matrix->n_filters; i++) {
        int len = strlen(matrix->rows[i].filter_name);
        if (len > filter_col_width) {
            filter_col_width = len;
        }
    }
    
    // Test categories with their indices
    typedef struct {
        const char* name;
        int start_idx;
        int end_idx;
    } TestGroup_t;
    
    TestGroup_t groups[10];
    int n_groups = 0;
    
    // Build test groups
    const char* current_category = NULL;
    for (int i = 0; i < matrix->n_tests; i++) {
        const char* category = get_test_category(matrix->tests[i].name);
        if (!current_category || strcmp(category, current_category) != 0) {
            if (n_groups > 0) {
                groups[n_groups - 1].end_idx = i - 1;
            }
            groups[n_groups].name = category;
            groups[n_groups].start_idx = i;
            groups[n_groups].end_idx = i;  // Will be updated
            n_groups++;
            current_category = category;
        }
    }
    if (n_groups > 0) {
        groups[n_groups - 1].end_idx = matrix->n_tests - 1;
    }
    
    // Print each group
    for (int g = 0; g < n_groups; g++) {
        printf("--- %s Tests ---\n", groups[g].name);
        
        // Table header for this group
        printf("%-*s |", filter_col_width, "Filter");
        for (int i = groups[g].start_idx; i <= groups[g].end_idx; i++) {
            // Use even shorter names for grouped display
            char header[9];
            strncpy(header, matrix->tests[i].short_name, 8);
            header[8] = '\0';
            printf(" %-8s|", header);
        }
        printf(" Subtotal\n");
        
        // Separator line
        for (int i = 0; i < filter_col_width; i++) printf("-");
        printf("-|");
        for (int i = groups[g].start_idx; i <= groups[g].end_idx; i++) {
            printf("---------|");
        }
        printf("---------\n");
        
        // Filter rows for this group
        for (int f = 0; f < matrix->n_filters; f++) {
            FilterTestRow_t* row = &matrix->rows[f];
            printf("%-*s |", filter_col_width, row->filter_name);
            
            int group_pass = 0, group_fail = 0, group_skip = 0;
            
            for (int i = groups[g].start_idx; i <= groups[g].end_idx; i++) {
                const char* result_str = compliance_result_to_string(row->results[i]);
                
                // Track group statistics
                switch (row->results[i]) {
                    case TEST_RESULT_PASS: group_pass++; break;
                    case TEST_RESULT_FAIL: group_fail++; break;
                    case TEST_RESULT_SKIP: group_skip++; break;
                    default: break;
                }
                
                if (use_color) {
                    switch (row->results[i]) {
                        case TEST_RESULT_PASS:
                            printf(" %s%-8s%s|", COLOR_GREEN, result_str, COLOR_RESET);
                            break;
                        case TEST_RESULT_FAIL:
                            printf(" %s%-8s%s|", COLOR_RED, result_str, COLOR_RESET);
                            break;
                        case TEST_RESULT_SKIP:
                            printf(" %s%-8s%s|", COLOR_YELLOW, result_str, COLOR_RESET);
                            break;
                        default:
                            printf(" %s%-8s%s|", COLOR_GRAY, result_str, COLOR_RESET);
                            break;
                    }
                } else {
                    printf(" %-8s|", result_str);
                }
            }
            
            // Group subtotal
            int group_total = groups[g].end_idx - groups[g].start_idx + 1;
            printf(" %d/%d\n", group_pass, group_total);
        }
        
        printf("\n");
    }
    
    // Overall summary
    printf("--- OVERALL SUMMARY ---\n");
    printf("%-*s | Pass  | Fail  | Skip  | Score\n", filter_col_width, "Filter");
    for (int i = 0; i < filter_col_width; i++) printf("-");
    printf("-|-------|-------|-------|-------\n");
    
    for (int i = 0; i < matrix->n_filters; i++) {
        FilterTestRow_t* row = &matrix->rows[i];
        printf("%-*s | %-5d | %-5d | %-5d | %d/%d\n",
               filter_col_width, row->filter_name,
               row->pass_count, row->fail_count, row->skip_count,
               row->pass_count, matrix->n_tests);
    }
    
    // Grand total
    int total_pass = 0, total_fail = 0, total_skip = 0;
    for (int i = 0; i < matrix->n_filters; i++) {
        total_pass += matrix->rows[i].pass_count;
        total_fail += matrix->rows[i].fail_count;
        total_skip += matrix->rows[i].skip_count;
    }
    
    for (int i = 0; i < filter_col_width; i++) printf("-");
    printf("-|-------|-------|-------|-------\n");
    
    int total_tests = matrix->n_filters * matrix->n_tests;
    printf("%-*s | %-5d | %-5d | %-5d | %.1f%%\n",
           filter_col_width, "TOTAL",
           total_pass, total_fail, total_skip,
           total_tests > 0 ? (100.0 * total_pass) / total_tests : 0);
    
    printf("\nLegend: PASS = Test passed | FAIL = Test failed | SKIP = Test skipped | N/A = Not applicable\n");
    
    return Bp_EC_OK;
}
Bp_EC compliance_matrix_write_grouped_csv(const ComplianceMatrix_t* matrix,
                                          const char* filename)
{
    if (!matrix || !filename) {
        return Bp_EC_INVALID_CONFIG;
    }
    
    FILE* fp = fopen(filename, "w");
    if (!fp) {
        return Bp_EC_INVALID_DATA;
    }
    
    // Write metadata header
    fprintf(fp, "# FILTER COMPLIANCE TEST RESULTS - GROUPED FORMAT\n");
    fprintf(fp, "# Generated: %s\n", matrix->timestamp);
    fprintf(fp, "# Commit: %s\n", matrix->commit_hash);
    fprintf(fp, "# Suite Version: %s\n", matrix->suite_version);
    fprintf(fp, "# Total Filters: %d\n", matrix->n_filters);
    fprintf(fp, "# Total Tests: %d\n", matrix->n_tests);
    fprintf(fp, "#\n");
    
    // Calculate column widths
    int filter_col_width = 20;
    for (int i = 0; i < matrix->n_filters; i++) {
        int len = strlen(matrix->rows[i].filter_name);
        if (len > filter_col_width) {
            filter_col_width = len;
        }
    }
    
    // Define test groups
    typedef struct {
        const char* name;
        const char* tests[10];  // Max tests per group
        int count;
    } TestGroup_t;
    
    TestGroup_t groups[] = {
        {"Lifecycle Tests", {}, 0},
        {"Connection Tests", {}, 0},
        {"Data Flow Tests", {}, 0},
        {"Error Handling Tests", {}, 0},
        {"Threading Tests", {}, 0},
        {"Performance Tests", {}, 0},
        {"Buffer Config Tests", {}, 0},
        {"Behavioral Tests", {}, 0}
    };
    
    // Categorize tests into groups
    for (int i = 0; i < matrix->n_tests; i++) {
        const char* category = get_test_category(matrix->tests[i].name);
        TestGroup_t* group = NULL;
        
        if (strcmp(category, "Lifecycle") == 0) group = &groups[0];
        else if (strcmp(category, "Connection") == 0) group = &groups[1];
        else if (strcmp(category, "Data Flow") == 0) group = &groups[2];
        else if (strcmp(category, "Error Handling") == 0) group = &groups[3];
        else if (strcmp(category, "Threading") == 0) group = &groups[4];
        else if (strcmp(category, "Performance") == 0) group = &groups[5];
        else if (strcmp(category, "Buffer Config") == 0) group = &groups[6];
        else if (strcmp(category, "Behavioral") == 0) group = &groups[7];
        
        if (group && group->count < 10) {
            group->tests[group->count++] = matrix->tests[i].short_name;
        }
    }
    
    // Write each test group as a separate table
    for (int g = 0; g < 8; g++) {
        if (groups[g].count == 0) continue;
        
        fprintf(fp, "\n# %s\n", groups[g].name);
        fprintf(fp, "%-*s", filter_col_width, "Filter");
        
        // Write test headers for this group
        for (int t = 0; t < groups[g].count; t++) {
            fprintf(fp, ",%-10s", groups[g].tests[t]);
        }
        fprintf(fp, ",Pass,Fail,Skip,Total\n");
        
        // Write filter results for this group
        for (int f = 0; f < matrix->n_filters; f++) {
            FilterTestRow_t* row = &matrix->rows[f];
            fprintf(fp, "%-*s", filter_col_width, row->filter_name);
            
            int group_pass = 0, group_fail = 0, group_skip = 0;
            
            // Find and write results for tests in this group
            for (int t = 0; t < groups[g].count; t++) {
                // Find the test index by short name
                int test_idx = -1;
                for (int i = 0; i < matrix->n_tests; i++) {
                    if (strcmp(matrix->tests[i].short_name, groups[g].tests[t]) == 0) {
                        test_idx = i;
                        break;
                    }
                }
                
                if (test_idx >= 0) {
                    TestResult_t result = row->results[test_idx];
                    fprintf(fp, ",%-10s", compliance_result_to_string(result));
                    switch (result) {
                        case TEST_RESULT_PASS: group_pass++; break;
                        case TEST_RESULT_FAIL: group_fail++; break;
                        case TEST_RESULT_SKIP: group_skip++; break;
                        default: break;
                    }
                }
            }
            
            fprintf(fp, ",%d,%d,%d,%d/%d\n", 
                    group_pass, group_fail, group_skip,
                    group_pass, groups[g].count);
        }
        
        // Write group totals
        fprintf(fp, "%-*s", filter_col_width, "TOTAL");
        for (int t = 0; t < groups[g].count; t++) {
            int test_idx = -1;
            for (int i = 0; i < matrix->n_tests; i++) {
                if (strcmp(matrix->tests[i].short_name, groups[g].tests[t]) == 0) {
                    test_idx = i;
                    break;
                }
            }
            
            if (test_idx >= 0) {
                int pass = 0;
                for (int f = 0; f < matrix->n_filters; f++) {
                    if (matrix->rows[f].results[test_idx] == TEST_RESULT_PASS) {
                        pass++;
                    }
                }
                fprintf(fp, ",%d/%d      ", pass, matrix->n_filters);
            }
        }
        
        // Calculate group totals
        int total_pass = 0, total_fail = 0, total_skip = 0;
        for (int f = 0; f < matrix->n_filters; f++) {
            for (int t = 0; t < groups[g].count; t++) {
                int test_idx = -1;
                for (int i = 0; i < matrix->n_tests; i++) {
                    if (strcmp(matrix->tests[i].short_name, groups[g].tests[t]) == 0) {
                        test_idx = i;
                        break;
                    }
                }
                if (test_idx >= 0) {
                    switch (matrix->rows[f].results[test_idx]) {
                        case TEST_RESULT_PASS: total_pass++; break;
                        case TEST_RESULT_FAIL: total_fail++; break;
                        case TEST_RESULT_SKIP: total_skip++; break;
                        default: break;
                    }
                }
            }
        }
        
        int group_total = matrix->n_filters * groups[g].count;
        fprintf(fp, ",%d,%d,%d,%d/%d\n", 
                total_pass, total_fail, total_skip,
                total_pass, group_total);
    }
    
    // Write overall summary
    fprintf(fp, "\n# OVERALL SUMMARY\n");
    fprintf(fp, "%-*s,Pass,Fail,Skip,Total,Pass%%\n", filter_col_width, "Filter");
    
    for (int i = 0; i < matrix->n_filters; i++) {
        FilterTestRow_t* row = &matrix->rows[i];
        float pass_pct = matrix->n_tests > 0 ? 
                         (100.0 * row->pass_count) / matrix->n_tests : 0;
        fprintf(fp, "%-*s,%d,%d,%d,%d/%d,%.1f%%\n",
                filter_col_width, row->filter_name,
                row->pass_count, row->fail_count, row->skip_count,
                row->pass_count, matrix->n_tests,
                pass_pct);
    }
    
    // Grand total
    int total_pass = 0, total_fail = 0, total_skip = 0;
    for (int i = 0; i < matrix->n_filters; i++) {
        total_pass += matrix->rows[i].pass_count;
        total_fail += matrix->rows[i].fail_count;
        total_skip += matrix->rows[i].skip_count;
    }
    int total_tests = matrix->n_filters * matrix->n_tests;
    float total_pass_pct = total_tests > 0 ? 
                           (100.0 * total_pass) / total_tests : 0;
    
    fprintf(fp, "%-*s,%d,%d,%d,%d/%d,%.1f%%\n",
            filter_col_width, "TOTAL",
            total_pass, total_fail, total_skip,
            total_pass, total_tests,
            total_pass_pct);
    
    fclose(fp);
    return Bp_EC_OK;
}
