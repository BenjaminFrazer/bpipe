#ifndef TEST_BENCH_UTILS_H
#define TEST_BENCH_UTILS_H

#include "core.h"
#include "batch_buffer.h"
#include <stdbool.h>

// Connection tracking
typedef struct {
    Filter_t* src;
    int src_port;
    Filter_t* dst;
    int dst_port;
} Connection_t;

// Test pipeline management
typedef struct {
    Filter_t** filters;
    size_t n_filters;
    size_t filters_capacity;
    
    Connection_t* connections;
    size_t n_connections;
    size_t connections_capacity;
    
    bool started;
} TestPipeline_t;

// Pipeline management functions
TestPipeline_t* test_pipeline_create(void);
void test_pipeline_destroy(TestPipeline_t* p);

// Add filters to pipeline (takes ownership)
Bp_EC test_pipeline_add_filter(TestPipeline_t* p, Filter_t* f);

// Connect filters
Bp_EC test_pipeline_connect(TestPipeline_t* p, 
                           Filter_t* src, int src_port,
                           Filter_t* dst, int dst_port);

// Lifecycle management
Bp_EC test_pipeline_start_all(TestPipeline_t* p);
Bp_EC test_pipeline_stop_all(TestPipeline_t* p);
Bp_EC test_pipeline_wait_all(TestPipeline_t* p);

// Verification utilities
typedef struct {
    size_t total_samples;
    size_t missing_samples;
    size_t duplicate_samples;
    size_t out_of_order_samples;
    uint32_t first_sequence;
    uint32_t last_sequence;
    bool sequence_valid;
} SequenceValidation_t;

// Validate sequential data integrity
SequenceValidation_t validate_sequence_data(float* data, size_t n_samples, 
                                          uint32_t expected_start);

// Timing validation
typedef struct {
    uint64_t min_period_ns;
    uint64_t max_period_ns;
    uint64_t avg_period_ns;
    size_t timing_violations;
    double jitter_percent;
} TimingValidation_t;

TimingValidation_t validate_timing(Batch_t** batches, size_t n_batches,
                                 uint64_t expected_period_ns);

// Performance measurement
typedef struct {
    uint64_t start_time_ns;
    uint64_t end_time_ns;
    size_t total_samples;
    size_t total_batches;
    double throughput_samples_per_sec;
    double throughput_batches_per_sec;
    uint64_t avg_latency_ns;
    uint64_t max_latency_ns;
    uint64_t min_latency_ns;
} PerformanceMetrics_t;

void perf_metrics_start(PerformanceMetrics_t* pm);
void perf_metrics_end(PerformanceMetrics_t* pm);
void perf_metrics_update_sample_count(PerformanceMetrics_t* pm, size_t samples);
void perf_metrics_update_batch_count(PerformanceMetrics_t* pm, size_t batches);
void perf_metrics_update_latency(PerformanceMetrics_t* pm, uint64_t latency_ns);
void perf_metrics_calculate(PerformanceMetrics_t* pm);

// Test helpers
bool wait_for_condition(bool (*condition)(void* ctx), void* ctx, 
                       uint64_t timeout_ms);

// Memory tracking
typedef struct {
    size_t initial_rss;
    size_t peak_rss;
    size_t final_rss;
    bool leak_detected;
} MemoryMetrics_t;

void memory_metrics_start(MemoryMetrics_t* mm);
void memory_metrics_update(MemoryMetrics_t* mm);
void memory_metrics_end(MemoryMetrics_t* mm);

// Thread monitoring
size_t get_thread_count(void);

// Test result reporting
void print_test_summary(const char* test_name, bool passed,
                       PerformanceMetrics_t* perf,
                       MemoryMetrics_t* mem);

#endif // TEST_BENCH_UTILS_H