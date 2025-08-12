#ifndef MOCK_FILTERS_H
#define MOCK_FILTERS_H

#include "core.h"
#include "batch_buffer.h"
#include <stdatomic.h>
#include <stdbool.h>

// Controllable Producer Filter
typedef enum {
    PATTERN_SEQUENTIAL,
    PATTERN_RANDOM,
    PATTERN_SINE,
    PATTERN_CONSTANT
} ProducerPattern_t;

typedef struct {
    const char* name;
    long timeout_us;
    size_t samples_per_second;     // Data generation rate
    ProducerPattern_t pattern;     // Data pattern to generate
    float constant_value;          // For PATTERN_CONSTANT
    float sine_frequency;          // For PATTERN_SINE
    size_t max_batches;           // Stop after this many batches (0 = infinite)
    bool burst_mode;              // Enable burst mode
    size_t burst_on_batches;      // Batches to produce in burst
    size_t burst_off_batches;     // Batches to pause in burst
    uint32_t start_sequence;      // Starting sequence number
} ControllableProducerConfig_t;

typedef struct {
    Filter_t base;
    
    // Configuration
    size_t samples_per_second;
    ProducerPattern_t pattern;
    float constant_value;
    float sine_frequency;
    size_t max_batches;
    bool burst_mode;
    size_t burst_on_batches;
    size_t burst_off_batches;
    uint32_t start_sequence;
    
    // Runtime state
    atomic_size_t batches_produced;
    atomic_size_t samples_generated;
    atomic_uint_fast64_t last_timestamp_ns;
    size_t burst_counter;
    bool in_burst_on_phase;
    uint32_t next_sequence;
    float sine_phase;
    
    // Metrics
    atomic_size_t total_batches;
    atomic_size_t total_samples;
    atomic_size_t dropped_batches;  // For DROP mode
} ControllableProducer_t;

// Controllable Consumer Filter
typedef struct {
    const char* name;
    BatchBuffer_config buff_config;
    long timeout_us;
    size_t process_delay_us;      // Microseconds to "process" each batch
    bool validate_sequence;       // Check for sequential data
    bool validate_timing;         // Check timestamp accuracy
    size_t consume_pattern;       // 0=steady, N=consume N then pause N
    bool slow_start;             // Start slow, speed up over time
    size_t slow_start_batches;   // Number of batches for slow start
} ControllableConsumerConfig_t;

typedef struct {
    Filter_t base;
    
    // Configuration  
    size_t process_delay_us;
    bool validate_sequence;
    bool validate_timing;
    size_t consume_pattern;
    bool slow_start;
    size_t slow_start_batches;
    
    // Runtime state
    atomic_size_t batches_consumed;
    atomic_size_t samples_consumed;
    uint32_t expected_sequence;
    uint64_t last_timestamp_ns;
    size_t pattern_counter;
    bool in_consume_phase;
    
    // Metrics
    atomic_size_t total_batches;
    atomic_size_t total_samples;
    atomic_size_t sequence_errors;
    atomic_size_t timing_errors;
    atomic_uint_fast64_t total_latency_ns;
    atomic_uint_fast64_t max_latency_ns;
    atomic_uint_fast64_t min_latency_ns;
} ControllableConsumer_t;

// Passthrough Filter with Metrics
typedef struct {
    const char* name;
    BatchBuffer_config buff_config;
    long timeout_us;
    bool measure_latency;
    bool measure_queue_depth;
} PassthroughMetricsConfig_t;

typedef struct {
    Filter_t base;
    
    // Configuration
    bool measure_latency;
    bool measure_queue_depth;
    
    // Metrics
    atomic_size_t batches_processed;
    atomic_size_t samples_processed;
    atomic_uint_fast64_t total_latency_ns;
    atomic_uint_fast64_t max_latency_ns;
    atomic_uint_fast64_t min_latency_ns;
    atomic_size_t max_queue_depth;
    atomic_size_t current_queue_depth;
} PassthroughMetrics_t;

// Variable Batch Producer Filter - for testing partial batch handling
typedef struct {
    const char* name;
    long timeout_us;
    uint32_t* batch_sizes;         // Array of batch sizes to produce
    size_t n_batch_sizes;          // Number of batch sizes in array
    bool cycle_batch_sizes;        // Loop through sizes or stop after one pass
    ProducerPattern_t pattern;     // Data pattern to fill batches
    uint64_t sample_period_ns;     // Timing metadata for batches
    uint32_t start_sequence;       // Starting sequence number for PATTERN_SEQUENTIAL
} VariableBatchProducerConfig_t;

typedef struct {
    Filter_t base;
    
    // Configuration
    uint32_t* batch_sizes;
    size_t n_batch_sizes;
    bool cycle_batch_sizes;
    ProducerPattern_t pattern;
    uint64_t sample_period_ns;
    uint32_t start_sequence;
    
    // Runtime state
    size_t current_batch_index;
    uint32_t next_sequence;
    float sine_phase;
    uint64_t next_batch_time_ns;
    
    // Metrics
    atomic_size_t total_batches;
    atomic_size_t total_samples;
    atomic_size_t cycles_completed;
} VariableBatchProducer_t;

// Error Injection Filter
typedef enum {
    ERROR_NONE,
    ERROR_WORKER_ASSERT,
    ERROR_TIMEOUT,
    ERROR_ALLOC,
    ERROR_RANDOM,
    ERROR_AFTER_N_BATCHES
} ErrorInjectionType_t;

typedef struct {
    const char* name;
    BatchBuffer_config buff_config;
    long timeout_us;
    ErrorInjectionType_t error_type;
    Bp_EC error_code;             // Error code to inject
    size_t error_after_batches;   // Inject after N batches
    float error_probability;      // For ERROR_RANDOM (0.0-1.0)
} ErrorInjectionConfig_t;

typedef struct {
    Filter_t base;
    
    // Configuration
    ErrorInjectionType_t error_type;
    Bp_EC error_code;
    size_t error_after_batches;
    float error_probability;
    
    // Runtime state
    atomic_size_t batches_before_error;
    atomic_bool error_injected;
} ErrorInjection_t;

// Public APIs
Bp_EC controllable_producer_init(ControllableProducer_t* cp, ControllableProducerConfig_t config);
Bp_EC controllable_consumer_init(ControllableConsumer_t* cc, ControllableConsumerConfig_t config);
Bp_EC passthrough_metrics_init(PassthroughMetrics_t* pm, PassthroughMetricsConfig_t config);
Bp_EC error_injection_init(ErrorInjection_t* ei, ErrorInjectionConfig_t config);
Bp_EC variable_batch_producer_init(VariableBatchProducer_t* vbp, VariableBatchProducerConfig_t config);

// Metrics getters
void controllable_producer_get_metrics(ControllableProducer_t* cp, 
                                     size_t* batches, 
                                     size_t* samples,
                                     size_t* dropped);

void controllable_consumer_get_metrics(ControllableConsumer_t* cc,
                                     size_t* batches,
                                     size_t* samples,
                                     size_t* seq_errors,
                                     size_t* timing_errors,
                                     uint64_t* avg_latency_ns);

void passthrough_metrics_get_metrics(PassthroughMetrics_t* pm,
                                   size_t* batches,
                                   size_t* samples,
                                   uint64_t* avg_latency_ns,
                                   size_t* max_queue);

void variable_batch_producer_get_metrics(VariableBatchProducer_t* vbp,
                                       size_t* batches,
                                       size_t* samples,
                                       size_t* cycles);

#endif // MOCK_FILTERS_H