#define _DEFAULT_SOURCE
#include "test_bench_utils.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/resource.h>
#include <dirent.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>

// Initial capacities
#define INITIAL_FILTER_CAPACITY 16
#define INITIAL_CONNECTION_CAPACITY 32

// Pipeline management
TestPipeline_t* test_pipeline_create(void) {
    TestPipeline_t* p = calloc(1, sizeof(TestPipeline_t));
    if (!p) return NULL;
    
    p->filters = calloc(INITIAL_FILTER_CAPACITY, sizeof(Filter_t*));
    if (!p->filters) {
        free(p);
        return NULL;
    }
    p->filters_capacity = INITIAL_FILTER_CAPACITY;
    
    p->connections = calloc(INITIAL_CONNECTION_CAPACITY, sizeof(Connection_t));
    if (!p->connections) {
        free(p->filters);
        free(p);
        return NULL;
    }
    p->connections_capacity = INITIAL_CONNECTION_CAPACITY;
    
    p->started = false;
    return p;
}

void test_pipeline_destroy(TestPipeline_t* p) {
    if (!p) return;
    
    // Stop all filters if still running
    if (p->started) {
        test_pipeline_stop_all(p);
    }
    
    // Clean up filters
    for (size_t i = 0; i < p->n_filters; i++) {
        if (p->filters[i]) {
            filt_deinit(p->filters[i]);
        }
    }
    
    free(p->filters);
    free(p->connections);
    free(p);
}

Bp_EC test_pipeline_add_filter(TestPipeline_t* p, Filter_t* f) {
    if (!p || !f) return Bp_EC_NULL_POINTER;
    
    // Resize if needed
    if (p->n_filters >= p->filters_capacity) {
        size_t new_capacity = p->filters_capacity * 2;
        Filter_t** new_filters = realloc(p->filters, 
                                       new_capacity * sizeof(Filter_t*));
        if (!new_filters) return Bp_EC_ALLOC;
        p->filters = new_filters;
        p->filters_capacity = new_capacity;
    }
    
    p->filters[p->n_filters++] = f;
    return Bp_EC_OK;
}

Bp_EC test_pipeline_connect(TestPipeline_t* p, 
                           Filter_t* src, int src_port,
                           Filter_t* dst, int dst_port) {
    if (!p || !src || !dst) return Bp_EC_NULL_POINTER;
    if (p->started) return Bp_EC_INVALID_CONFIG;
    
    // Perform the actual connection
    Bp_EC err = filt_sink_connect(src, src_port, dst->input_buffers[dst_port]);
    if (err != Bp_EC_OK) return err;
    
    // Record the connection
    if (p->n_connections >= p->connections_capacity) {
        size_t new_capacity = p->connections_capacity * 2;
        Connection_t* new_conns = realloc(p->connections,
                                        new_capacity * sizeof(Connection_t));
        if (!new_conns) return Bp_EC_ALLOC;
        p->connections = new_conns;
        p->connections_capacity = new_capacity;
    }
    
    Connection_t* conn = &p->connections[p->n_connections++];
    conn->src = src;
    conn->src_port = src_port;
    conn->dst = dst;
    conn->dst_port = dst_port;
    
    return Bp_EC_OK;
}

Bp_EC test_pipeline_start_all(TestPipeline_t* p) {
    if (!p) return Bp_EC_NULL_POINTER;
    if (p->started) return Bp_EC_INVALID_CONFIG;
    
    // Start all filters
    for (size_t i = 0; i < p->n_filters; i++) {
        Bp_EC err = filt_start(p->filters[i]);
        if (err != Bp_EC_OK) {
            // Stop any already started filters
            for (size_t j = 0; j < i; j++) {
                filt_stop(p->filters[j]);
            }
            return err;
        }
    }
    
    p->started = true;
    return Bp_EC_OK;
}

Bp_EC test_pipeline_stop_all(TestPipeline_t* p) {
    if (!p) return Bp_EC_NULL_POINTER;
    if (!p->started) return Bp_EC_OK;
    
    // Stop all filters in reverse order
    for (int i = p->n_filters - 1; i >= 0; i--) {
        filt_stop(p->filters[i]);
    }
    
    p->started = false;
    return Bp_EC_OK;
}

Bp_EC test_pipeline_wait_all(TestPipeline_t* p) {
    if (!p) return Bp_EC_NULL_POINTER;
    
    // Wait for all worker threads to finish
    for (size_t i = 0; i < p->n_filters; i++) {
        Filter_t* f = p->filters[i];
        if (!f || !f->worker) continue;
        
        // Only join threads that were actually created
        // Check if the filter's running flag was ever set (indicating thread creation)
        // Note: We can't check p->started because stop_all sets it to false
        // Instead, we'll just try to join and handle any errors
        int ret = pthread_join(f->worker_thread, NULL);
        // Ignore ESRCH (no such thread) - this means thread was never created or already joined
        if (ret != 0 && ret != ESRCH) {
            return Bp_EC_PTHREAD_UNKOWN;
        }
    }
    
    return Bp_EC_OK;
}

// Sequence validation
SequenceValidation_t validate_sequence_data(float* data, size_t n_samples, 
                                          uint32_t expected_start) {
    SequenceValidation_t result = {0};
    result.total_samples = n_samples;
    result.sequence_valid = true;
    
    if (n_samples == 0) return result;
    
    result.first_sequence = (uint32_t)data[0];
    result.last_sequence = (uint32_t)data[n_samples - 1];
    
    uint32_t expected = expected_start;
    for (size_t i = 0; i < n_samples; i++) {
        uint32_t actual = (uint32_t)data[i];
        
        if (actual != expected) {
            if (actual < expected) {
                result.duplicate_samples++;
            } else if (actual > expected + 1) {
                result.missing_samples += (actual - expected - 1);
            }
            result.out_of_order_samples++;
            result.sequence_valid = false;
            expected = actual;
        }
        expected++;
    }
    
    return result;
}

// Timing validation
TimingValidation_t validate_timing(Batch_t** batches, size_t n_batches,
                                 uint64_t expected_period_ns) {
    TimingValidation_t result = {0};
    result.min_period_ns = UINT64_MAX;
    
    if (n_batches < 2) return result;
    
    uint64_t total_period = 0;
    uint64_t max_deviation = 0;
    
    for (size_t i = 1; i < n_batches; i++) {
        uint64_t period = batches[i]->t_ns - batches[i-1]->t_ns;
        
        if (period < result.min_period_ns) {
            result.min_period_ns = period;
        }
        if (period > result.max_period_ns) {
            result.max_period_ns = period;
        }
        
        total_period += period;
        
        uint64_t deviation = (period > expected_period_ns) ?
                           (period - expected_period_ns) :
                           (expected_period_ns - period);
        
        if (deviation > max_deviation) {
            max_deviation = deviation;
        }
        
        // Count violations (> 10% deviation)
        if (deviation > expected_period_ns / 10) {
            result.timing_violations++;
        }
    }
    
    result.avg_period_ns = total_period / (n_batches - 1);
    result.jitter_percent = (double)max_deviation / expected_period_ns * 100.0;
    
    return result;
}

// Performance metrics
void perf_metrics_start(PerformanceMetrics_t* pm) {
    memset(pm, 0, sizeof(*pm));
    pm->min_latency_ns = UINT64_MAX;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    pm->start_time_ns = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

void perf_metrics_end(PerformanceMetrics_t* pm) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    pm->end_time_ns = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

void perf_metrics_update_sample_count(PerformanceMetrics_t* pm, size_t samples) {
    pm->total_samples += samples;
}

void perf_metrics_update_batch_count(PerformanceMetrics_t* pm, size_t batches) {
    pm->total_batches += batches;
}

void perf_metrics_update_latency(PerformanceMetrics_t* pm, uint64_t latency_ns) {
    pm->avg_latency_ns = (pm->avg_latency_ns * pm->total_batches + latency_ns) / 
                        (pm->total_batches + 1);
    
    if (latency_ns > pm->max_latency_ns) {
        pm->max_latency_ns = latency_ns;
    }
    if (latency_ns < pm->min_latency_ns) {
        pm->min_latency_ns = latency_ns;
    }
}

void perf_metrics_calculate(PerformanceMetrics_t* pm) {
    uint64_t duration_ns = pm->end_time_ns - pm->start_time_ns;
    double duration_sec = duration_ns / 1e9;
    
    pm->throughput_samples_per_sec = pm->total_samples / duration_sec;
    pm->throughput_batches_per_sec = pm->total_batches / duration_sec;
}

// Wait for condition with timeout
bool wait_for_condition(bool (*condition)(void* ctx), void* ctx, 
                       uint64_t timeout_ms) {
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    while (!condition(ctx)) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t elapsed_ms = ((now.tv_sec - start.tv_sec) * 1000) +
                             ((now.tv_nsec - start.tv_nsec) / 1000000);
        
        if (elapsed_ms >= timeout_ms) {
            return false;
        }
        
        usleep(1000); // 1ms sleep
    }
    
    return true;
}

// Memory metrics
static size_t get_rss_kb(void) {
    FILE* file = fopen("/proc/self/status", "r");
    if (!file) return 0;
    
    char line[256];
    size_t rss_kb = 0;
    
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "VmRSS: %zu kB", &rss_kb) == 1) {
            break;
        }
    }
    
    fclose(file);
    return rss_kb;
}

void memory_metrics_start(MemoryMetrics_t* mm) {
    memset(mm, 0, sizeof(*mm));
    mm->initial_rss = get_rss_kb();
    mm->peak_rss = mm->initial_rss;
}

void memory_metrics_update(MemoryMetrics_t* mm) {
    size_t current = get_rss_kb();
    if (current > mm->peak_rss) {
        mm->peak_rss = current;
    }
}

void memory_metrics_end(MemoryMetrics_t* mm) {
    mm->final_rss = get_rss_kb();
    // Simple leak detection: final > initial + 10%
    mm->leak_detected = (mm->final_rss > mm->initial_rss * 1.1);
}

// Thread count
size_t get_thread_count(void) {
    DIR* dir = opendir("/proc/self/task");
    if (!dir) return 0;
    
    size_t count = 0;
    struct dirent* entry;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.') {
            count++;
        }
    }
    
    closedir(dir);
    return count;
}

// Test reporting
void print_test_summary(const char* test_name, bool passed,
                       PerformanceMetrics_t* perf,
                       MemoryMetrics_t* mem) {
    printf("\n========== Test Summary: %s ==========\n", test_name);
    printf("Result: %s\n", passed ? "PASSED" : "FAILED");
    
    if (perf && perf->end_time_ns > perf->start_time_ns) {
        printf("\nPerformance Metrics:\n");
        printf("  Total samples: %zu\n", perf->total_samples);
        printf("  Total batches: %zu\n", perf->total_batches);
        printf("  Throughput: %.2f Msamples/sec\n", 
               perf->throughput_samples_per_sec / 1e6);
        printf("  Batch rate: %.2f batches/sec\n", 
               perf->throughput_batches_per_sec);
        printf("  Latency - Avg: %.3f ms, Max: %.3f ms, Min: %.3f ms\n",
               perf->avg_latency_ns / 1e6,
               perf->max_latency_ns / 1e6,
               perf->min_latency_ns / 1e6);
    }
    
    if (mem) {
        printf("\nMemory Metrics:\n");
        printf("  Initial RSS: %zu KB\n", mem->initial_rss);
        printf("  Peak RSS: %zu KB\n", mem->peak_rss);
        printf("  Final RSS: %zu KB\n", mem->final_rss);
        printf("  Leak detected: %s\n", mem->leak_detected ? "YES" : "NO");
    }
    
    printf("=====================================\n");
}