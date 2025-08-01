#define _DEFAULT_SOURCE
#include "mock_filters.h"
#include "utils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>

// Helper to get current time in nanoseconds
static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// Controllable Producer Implementation
static void* controllable_producer_worker(void* arg) {
    ControllableProducer_t* cp = (ControllableProducer_t*)arg;
    BP_WORKER_ASSERT(&cp->base, cp->base.sinks[0] != NULL, Bp_EC_NO_SINK);
    
    uint64_t period_ns = 1000000000ULL / cp->samples_per_second;
    uint64_t batch_period_ns = period_ns * cp->batch_size;
    uint64_t next_batch_time = get_time_ns();
    
    while (atomic_load(&cp->base.running)) {
        // Check if we've hit max batches
        if (cp->max_batches > 0 && 
            atomic_load(&cp->batches_produced) >= cp->max_batches) {
            // Send completion signal
            Batch_t* completion = bb_get_head(cp->base.sinks[0]);
            completion->ec = Bp_EC_COMPLETE;
            completion->head = 0;
            Bp_EC err = bb_submit(cp->base.sinks[0], cp->base.timeout_us);
            BP_WORKER_ASSERT(&cp->base, err == Bp_EC_OK, err);
            break;
        }
        
        // Handle burst mode
        if (cp->burst_mode) {
            if (cp->in_burst_on_phase) {
                if (++cp->burst_counter >= cp->burst_on_batches) {
                    cp->in_burst_on_phase = false;
                    cp->burst_counter = 0;
                }
            } else {
                if (++cp->burst_counter >= cp->burst_off_batches) {
                    cp->in_burst_on_phase = true;
                    cp->burst_counter = 0;
                } else {
                    // In off phase, sleep
                    usleep(batch_period_ns / 1000);
                    continue;
                }
            }
        }
        
        // Get output batch
        Batch_t* output = bb_get_head(cp->base.sinks[0]);
        
        // Generate data based on pattern
        float* data = (float*)output->data;
        for (size_t i = 0; i < cp->batch_size; i++) {
            switch (cp->pattern) {
                case PATTERN_SEQUENTIAL:
                    data[i] = (float)(cp->next_sequence++);
                    break;
                case PATTERN_CONSTANT:
                    data[i] = cp->constant_value;
                    break;
                case PATTERN_SINE:
                    data[i] = sinf(cp->sine_phase);
                    cp->sine_phase += 2.0f * M_PI * cp->sine_frequency / cp->samples_per_second;
                    if (cp->sine_phase > 2.0f * M_PI) {
                        cp->sine_phase -= 2.0f * M_PI;
                    }
                    break;
                case PATTERN_RANDOM:
                    data[i] = (float)rand() / RAND_MAX;
                    break;
            }
        }
        
        // Set batch metadata
        output->head = cp->batch_size;
        output->t_ns = next_batch_time;
        output->period_ns = period_ns;
        output->ec = Bp_EC_OK;
        
        // Submit batch
        Bp_EC err = bb_submit(cp->base.sinks[0], cp->base.timeout_us);
        if (err == Bp_EC_NO_SPACE) {
            atomic_fetch_add(&cp->dropped_batches, 1);
        } else {
            BP_WORKER_ASSERT(&cp->base, err == Bp_EC_OK, err);
        }
        
        // Update metrics
        atomic_fetch_add(&cp->batches_produced, 1);
        atomic_fetch_add(&cp->samples_generated, cp->batch_size);
        atomic_fetch_add(&cp->total_batches, 1);
        atomic_fetch_add(&cp->total_samples, cp->batch_size);
        atomic_store(&cp->last_timestamp_ns, next_batch_time);
        
        // Rate limiting
        uint64_t now = get_time_ns();
        next_batch_time += batch_period_ns;
        if (next_batch_time > now) {
            usleep((next_batch_time - now) / 1000);
        }
    }
    
    return NULL;
}

Bp_EC controllable_producer_init(ControllableProducer_t* cp, ControllableProducerConfig_t config) {
    if (!cp) return Bp_EC_NULL_POINTER;
    
    // Build core config
    Core_filt_config_t core_config = {
        .name = config.name,
        .filt_type = FILT_T_MAP,
        .size = sizeof(ControllableProducer_t),
        .n_inputs = 0,  // Source filter
        .max_supported_sinks = 1,
        .timeout_us = config.timeout_us,
        .worker = controllable_producer_worker
    };
    
    // Initialize base filter
    Bp_EC err = filt_init(&cp->base, core_config);
    if (err != Bp_EC_OK) return err;
    
    // Initialize configuration
    cp->samples_per_second = config.samples_per_second;
    cp->batch_size = config.batch_size;
    cp->pattern = config.pattern;
    cp->constant_value = config.constant_value;
    cp->sine_frequency = config.sine_frequency;
    cp->max_batches = config.max_batches;
    cp->burst_mode = config.burst_mode;
    cp->burst_on_batches = config.burst_on_batches;
    cp->burst_off_batches = config.burst_off_batches;
    cp->start_sequence = config.start_sequence;
    
    // Initialize runtime state
    atomic_store(&cp->batches_produced, 0);
    atomic_store(&cp->samples_generated, 0);
    atomic_store(&cp->last_timestamp_ns, 0);
    cp->burst_counter = 0;
    cp->in_burst_on_phase = true;
    cp->next_sequence = config.start_sequence;
    cp->sine_phase = 0.0f;
    
    // Initialize metrics
    atomic_store(&cp->total_batches, 0);
    atomic_store(&cp->total_samples, 0);
    atomic_store(&cp->dropped_batches, 0);
    
    return Bp_EC_OK;
}

// Controllable Consumer Implementation
static void* controllable_consumer_worker(void* arg) {
    ControllableConsumer_t* cc = (ControllableConsumer_t*)arg;
    BP_WORKER_ASSERT(&cc->base, cc->base.n_input_buffers == 1, Bp_EC_INVALID_CONFIG);
    
    while (atomic_load(&cc->base.running)) {
        // Handle consume pattern
        if (cc->consume_pattern > 0) {
            if (cc->in_consume_phase) {
                if (++cc->pattern_counter >= cc->consume_pattern) {
                    cc->in_consume_phase = false;
                    cc->pattern_counter = 0;
                }
            } else {
                if (++cc->pattern_counter >= cc->consume_pattern) {
                    cc->in_consume_phase = true;
                    cc->pattern_counter = 0;
                } else {
                    usleep(10000); // 10ms pause
                    continue;
                }
            }
        }
        
        // Get input batch
        Bp_EC err;
        Batch_t* input = bb_get_tail(cc->base.input_buffers[0], 
                                    cc->base.timeout_us, &err);
        if (!input) {
            if (err == Bp_EC_TIMEOUT) continue;
            if (err == Bp_EC_STOPPED) break;
            break;
        }
        
        // Check for completion
        if (input->ec == Bp_EC_COMPLETE) {
            bb_del_tail(cc->base.input_buffers[0]);
            break;
        }
        
        BP_WORKER_ASSERT(&cc->base, input->ec == Bp_EC_OK, input->ec);
        
        // Calculate processing delay
        size_t delay_us = cc->process_delay_us;
        if (cc->slow_start && atomic_load(&cc->batches_consumed) < cc->slow_start_batches) {
            size_t progress = atomic_load(&cc->batches_consumed);
            delay_us = cc->process_delay_us * (cc->slow_start_batches - progress) / cc->slow_start_batches;
        }
        
        // Validate sequence if enabled
        if (cc->validate_sequence) {
            float* data = (float*)input->data;
            for (size_t i = 0; i < input->head; i++) {
                if ((uint32_t)data[i] != cc->expected_sequence) {
                    atomic_fetch_add(&cc->sequence_errors, 1);
                }
                cc->expected_sequence++;
            }
        }
        
        // Validate timing if enabled
        if (cc->validate_timing && cc->last_timestamp_ns > 0) {
            uint64_t expected_time = cc->last_timestamp_ns + 
                                   (input->period_ns * input->head);
            int64_t timing_error = (int64_t)input->t_ns - (int64_t)expected_time;
            if (abs(timing_error) > 1000000) { // > 1ms error
                atomic_fetch_add(&cc->timing_errors, 1);
            }
        }
        cc->last_timestamp_ns = input->t_ns;
        
        // Calculate latency
        uint64_t now = get_time_ns();
        uint64_t latency = now - input->t_ns;
        atomic_fetch_add(&cc->total_latency_ns, latency);
        
        // Update min/max latency
        uint64_t max_lat = atomic_load(&cc->max_latency_ns);
        while (latency > max_lat && 
               !atomic_compare_exchange_weak(&cc->max_latency_ns, &max_lat, latency));
        
        uint64_t min_lat = atomic_load(&cc->min_latency_ns);
        while ((min_lat == 0 || latency < min_lat) && 
               !atomic_compare_exchange_weak(&cc->min_latency_ns, &min_lat, latency));
        
        // Simulate processing
        if (delay_us > 0) {
            usleep(delay_us);
        }
        
        // Update metrics
        atomic_fetch_add(&cc->batches_consumed, 1);
        atomic_fetch_add(&cc->samples_consumed, input->head);
        atomic_fetch_add(&cc->total_batches, 1);
        atomic_fetch_add(&cc->total_samples, input->head);
        
        // Return batch
        bb_del_tail(cc->base.input_buffers[0]);
    }
    
    return NULL;
}

Bp_EC controllable_consumer_init(ControllableConsumer_t* cc, ControllableConsumerConfig_t config) {
    if (!cc) return Bp_EC_NULL_POINTER;
    
    // Build core config
    Core_filt_config_t core_config = {
        .name = config.name,
        .filt_type = FILT_T_MAP,
        .size = sizeof(ControllableConsumer_t),
        .n_inputs = 1,
        .max_supported_sinks = 0,  // Sink filter
        .buff_config = config.buff_config,
        .timeout_us = config.timeout_us,
        .worker = controllable_consumer_worker
    };
    
    // Initialize base filter
    Bp_EC err = filt_init(&cc->base, core_config);
    if (err != Bp_EC_OK) return err;
    
    // Initialize configuration
    cc->process_delay_us = config.process_delay_us;
    cc->validate_sequence = config.validate_sequence;
    cc->validate_timing = config.validate_timing;
    cc->consume_pattern = config.consume_pattern;
    cc->slow_start = config.slow_start;
    cc->slow_start_batches = config.slow_start_batches;
    
    // Initialize runtime state
    atomic_store(&cc->batches_consumed, 0);
    atomic_store(&cc->samples_consumed, 0);
    cc->expected_sequence = 0;
    cc->last_timestamp_ns = 0;
    cc->pattern_counter = 0;
    cc->in_consume_phase = true;
    
    // Initialize metrics
    atomic_store(&cc->total_batches, 0);
    atomic_store(&cc->total_samples, 0);
    atomic_store(&cc->sequence_errors, 0);
    atomic_store(&cc->timing_errors, 0);
    atomic_store(&cc->total_latency_ns, 0);
    atomic_store(&cc->max_latency_ns, 0);
    atomic_store(&cc->min_latency_ns, 0);
    
    return Bp_EC_OK;
}

// Metrics getters
void controllable_producer_get_metrics(ControllableProducer_t* cp, 
                                     size_t* batches, 
                                     size_t* samples,
                                     size_t* dropped) {
    if (batches) *batches = atomic_load(&cp->total_batches);
    if (samples) *samples = atomic_load(&cp->total_samples);
    if (dropped) *dropped = atomic_load(&cp->dropped_batches);
}

void controllable_consumer_get_metrics(ControllableConsumer_t* cc,
                                     size_t* batches,
                                     size_t* samples,
                                     size_t* seq_errors,
                                     size_t* timing_errors,
                                     uint64_t* avg_latency_ns) {
    if (batches) *batches = atomic_load(&cc->total_batches);
    if (samples) *samples = atomic_load(&cc->total_samples);
    if (seq_errors) *seq_errors = atomic_load(&cc->sequence_errors);
    if (timing_errors) *timing_errors = atomic_load(&cc->timing_errors);
    if (avg_latency_ns) {
        size_t total_b = atomic_load(&cc->total_batches);
        if (total_b > 0) {
            *avg_latency_ns = atomic_load(&cc->total_latency_ns) / total_b;
        } else {
            *avg_latency_ns = 0;
        }
    }
}

// TODO: Implement PassthroughMetrics and ErrorInjection filters