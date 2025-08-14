#define _DEFAULT_SOURCE
#include "mock_filters.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "utils.h"

// Helper to get current time in nanoseconds
static uint64_t get_time_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t) ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// Controllable Producer Implementation
static void* controllable_producer_worker(void* arg)
{
  ControllableProducer_t* cp = (ControllableProducer_t*) arg;
  BP_WORKER_ASSERT(&cp->base, cp->base.sinks[0] != NULL, Bp_EC_NO_SINK);

  uint64_t period_ns = 1000000000ULL / cp->samples_per_second;
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
          // In off phase, sleep for one batch period
          // We'll calculate this later when we know the batch size
          usleep(10000);  // 10ms default pause
          continue;
        }
      }
    }

    // Get output batch
    Batch_t* output = bb_get_head(cp->base.sinks[0]);
    BP_WORKER_ASSERT(&cp->base, output != NULL, Bp_EC_NULL_POINTER);

    // Always use the sink's batch capacity
    size_t batch_size = bb_batch_size(cp->base.sinks[0]);
    BP_WORKER_ASSERT(&cp->base, batch_size > 0, Bp_EC_INVALID_CONFIG);
    BP_WORKER_ASSERT(&cp->base, batch_size <= 65536,
                     Bp_EC_INVALID_CONFIG);  // Sanity check

    // Safety check - ensure we have valid data pointer
    BP_WORKER_ASSERT(&cp->base, output->data != NULL, Bp_EC_NULL_POINTER);

    // Check that the sink buffer is configured for float data
    BP_WORKER_ASSERT(&cp->base, cp->base.sinks[0]->dtype == DTYPE_FLOAT,
                     Bp_EC_TYPE_MISMATCH);

    // Generate data based on pattern
    float* data = (float*) output->data;
    for (size_t i = 0; i < batch_size; i++) {
      switch (cp->pattern) {
        case PATTERN_SEQUENTIAL:
          data[i] = (float) (cp->next_sequence++);
          break;
        case PATTERN_CONSTANT:
          data[i] = cp->constant_value;
          break;
        case PATTERN_SINE:
          data[i] = sinf(cp->sine_phase);
          cp->sine_phase +=
              2.0f * M_PI * cp->sine_frequency / cp->samples_per_second;
          if (cp->sine_phase > 2.0f * M_PI) {
            cp->sine_phase -= 2.0f * M_PI;
          }
          break;
        case PATTERN_RANDOM:
          data[i] = (float) rand() / RAND_MAX;
          break;
      }
    }

    // Set batch metadata
    output->head = batch_size;
    output->t_ns = next_batch_time;
    output->period_ns = period_ns;
    output->ec = Bp_EC_OK;

    // Submit batch
    Bp_EC err = bb_submit(cp->base.sinks[0], cp->base.timeout_us);
    if (err == Bp_EC_NO_SPACE) {
      atomic_fetch_add(&cp->dropped_batches, 1);
    } else if (err == Bp_EC_FILTER_STOPPING) {
      break;  // Filter is stopping, exit gracefully
    } else {
      BP_WORKER_ASSERT(&cp->base, err == Bp_EC_OK, err);
    }

    // Update metrics
    atomic_fetch_add(&cp->batches_produced, 1);
    atomic_fetch_add(&cp->samples_generated, batch_size);
    atomic_fetch_add(&cp->total_batches, 1);
    atomic_fetch_add(&cp->total_samples, batch_size);
    atomic_store(&cp->last_timestamp_ns, next_batch_time);

    // Rate limiting - calculate batch period based on actual batch size
    uint64_t batch_period_ns = period_ns * batch_size;
    uint64_t now = get_time_ns();
    next_batch_time += batch_period_ns;
    if (next_batch_time > now) {
      usleep((next_batch_time - now) / 1000);
    }
  }

  return NULL;
}

Bp_EC controllable_producer_init(ControllableProducer_t* cp,
                                 ControllableProducerConfig_t config)
{
  if (!cp) return Bp_EC_NULL_POINTER;

  // Check if already initialized
  if (cp->base.filt_type != FILT_T_NDEF) {
    return Bp_EC_ALREADY_RUNNING;
  }

  // Build core config
  Core_filt_config_t core_config = {.name = config.name,
                                    .filt_type = FILT_T_MAP,
                                    .size = sizeof(ControllableProducer_t),
                                    .n_inputs = 0,  // Source filter
                                    .max_supported_sinks = 1,
                                    .timeout_us = config.timeout_us,
                                    .worker = controllable_producer_worker};

  // Initialize base filter
  Bp_EC err = filt_init(&cp->base, core_config);
  if (err != Bp_EC_OK) return err;

  // Initialize configuration
  cp->samples_per_second = config.samples_per_second;
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
static void* controllable_consumer_worker(void* arg)
{
  ControllableConsumer_t* cc = (ControllableConsumer_t*) arg;
  BP_WORKER_ASSERT(&cc->base, cc->base.n_input_buffers == 1,
                   Bp_EC_INVALID_CONFIG);

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
          usleep(10000);  // 10ms pause
          continue;
        }
      }
    }

    // Get input batch
    Bp_EC err;
    Batch_t* input =
        bb_get_tail(cc->base.input_buffers[0], cc->base.timeout_us, &err);
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
    if (cc->slow_start &&
        atomic_load(&cc->batches_consumed) < cc->slow_start_batches) {
      size_t progress = atomic_load(&cc->batches_consumed);
      delay_us = cc->process_delay_us * (cc->slow_start_batches - progress) /
                 cc->slow_start_batches;
    }

    // Validate sequence if enabled
    if (cc->validate_sequence) {
      float* data = (float*) input->data;
      for (size_t i = 0; i < input->head; i++) {
        if ((uint32_t) data[i] != cc->expected_sequence) {
          atomic_fetch_add(&cc->sequence_errors, 1);
        }
        cc->expected_sequence++;
      }
    }

    // Validate timing if enabled
    if (cc->validate_timing && cc->last_timestamp_ns > 0) {
      uint64_t expected_time =
          cc->last_timestamp_ns + (input->period_ns * input->head);
      int64_t timing_error = (int64_t) input->t_ns - (int64_t) expected_time;
      if (abs(timing_error) > 1000000) {  // > 1ms error
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
    while (latency > max_lat && !atomic_compare_exchange_weak(
                                    &cc->max_latency_ns, &max_lat, latency))
      ;

    uint64_t min_lat = atomic_load(&cc->min_latency_ns);
    while (
        (min_lat == 0 || latency < min_lat) &&
        !atomic_compare_exchange_weak(&cc->min_latency_ns, &min_lat, latency))
      ;

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

Bp_EC controllable_consumer_init(ControllableConsumer_t* cc,
                                 ControllableConsumerConfig_t config)
{
  if (!cc) return Bp_EC_NULL_POINTER;

  // Check if already initialized
  if (cc->base.filt_type != FILT_T_NDEF) {
    return Bp_EC_ALREADY_RUNNING;
  }

  // Build core config
  Core_filt_config_t core_config = {.name = config.name,
                                    .filt_type = FILT_T_MAP,
                                    .size = sizeof(ControllableConsumer_t),
                                    .n_inputs = 1,
                                    .max_supported_sinks = 0,  // Sink filter
                                    .buff_config = config.buff_config,
                                    .timeout_us = config.timeout_us,
                                    .worker = controllable_consumer_worker};

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
                                       size_t* batches, size_t* samples,
                                       size_t* dropped)
{
  if (batches) *batches = atomic_load(&cp->total_batches);
  if (samples) *samples = atomic_load(&cp->total_samples);
  if (dropped) *dropped = atomic_load(&cp->dropped_batches);
}

void controllable_consumer_get_metrics(ControllableConsumer_t* cc,
                                       size_t* batches, size_t* samples,
                                       size_t* seq_errors,
                                       size_t* timing_errors,
                                       uint64_t* avg_latency_ns)
{
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

// Passthrough Metrics Implementation
static void* passthrough_metrics_worker(void* arg)
{
  PassthroughMetrics_t* pm = (PassthroughMetrics_t*) arg;
  BP_WORKER_ASSERT(&pm->base, pm->base.n_input_buffers == 1,
                   Bp_EC_INVALID_CONFIG);
  BP_WORKER_ASSERT(&pm->base, pm->base.sinks[0] != NULL, Bp_EC_NO_SINK);

  while (atomic_load(&pm->base.running)) {
    // Measure queue depth if enabled
    if (pm->measure_queue_depth) {
      size_t depth = bb_occupancy(pm->base.input_buffers[0]);
      atomic_store(&pm->current_queue_depth, depth);

      // Update max queue depth
      size_t max_depth = atomic_load(&pm->max_queue_depth);
      while (depth > max_depth && !atomic_compare_exchange_weak(
                                      &pm->max_queue_depth, &max_depth, depth))
        ;
    }

    // Get input batch
    uint64_t receive_time = 0;
    if (pm->measure_latency) {
      receive_time = get_time_ns();
    }

    Bp_EC err;
    Batch_t* input =
        bb_get_tail(pm->base.input_buffers[0], pm->base.timeout_us, &err);
    if (!input) {
      if (err == Bp_EC_TIMEOUT) continue;
      if (err == Bp_EC_STOPPED) break;
      break;
    }

    // Check for completion
    if (input->ec == Bp_EC_COMPLETE) {
      // Pass through completion
      Batch_t* output = bb_get_head(pm->base.sinks[0]);
      *output = *input;  // Copy entire batch structure
      bb_submit(pm->base.sinks[0], pm->base.timeout_us);
      bb_del_tail(pm->base.input_buffers[0]);
      break;
    }

    // Get output batch
    Batch_t* output = bb_get_head(pm->base.sinks[0]);

    // Copy batch metadata
    output->t_ns = input->t_ns;
    output->period_ns = input->period_ns;
    output->head = input->head;
    output->ec = input->ec;
    output->batch_id = input->batch_id;

    // Copy data
    size_t data_size =
        bb_getdatawidth(pm->base.input_buffers[0]->dtype) * input->head;
    memcpy(output->data, input->data, data_size);

    // Measure latency if enabled
    if (pm->measure_latency && receive_time > 0) {
      uint64_t latency = receive_time - input->t_ns;
      atomic_fetch_add(&pm->total_latency_ns, latency);

      // Update min/max latency
      uint64_t max_lat = atomic_load(&pm->max_latency_ns);
      while (latency > max_lat && !atomic_compare_exchange_weak(
                                      &pm->max_latency_ns, &max_lat, latency))
        ;

      uint64_t min_lat = atomic_load(&pm->min_latency_ns);
      while (
          (min_lat == 0 || latency < min_lat) &&
          !atomic_compare_exchange_weak(&pm->min_latency_ns, &min_lat, latency))
        ;
    }

    // Submit output
    err = bb_submit(pm->base.sinks[0], pm->base.timeout_us);
    BP_WORKER_ASSERT(&pm->base, err == Bp_EC_OK, err);

    // Delete input
    err = bb_del_tail(pm->base.input_buffers[0]);
    BP_WORKER_ASSERT(&pm->base, err == Bp_EC_OK, err);

    // Update metrics
    atomic_fetch_add(&pm->batches_processed, 1);
    atomic_fetch_add(&pm->samples_processed, input->head);
  }

  return NULL;
}

Bp_EC passthrough_metrics_init(PassthroughMetrics_t* pm,
                               PassthroughMetricsConfig_t config)
{
  if (!pm) return Bp_EC_NULL_POINTER;

  // Build core config
  Core_filt_config_t core_config = {.name = config.name,
                                    .filt_type = FILT_T_MATCHED_PASSTHROUGH,
                                    .size = sizeof(PassthroughMetrics_t),
                                    .n_inputs = 1,
                                    .max_supported_sinks = 1,
                                    .buff_config = config.buff_config,
                                    .timeout_us = config.timeout_us,
                                    .worker = passthrough_metrics_worker};

  // Initialize base filter
  Bp_EC err = filt_init(&pm->base, core_config);
  if (err != Bp_EC_OK) return err;

  // Initialize configuration
  pm->measure_latency = config.measure_latency;
  pm->measure_queue_depth = config.measure_queue_depth;

  // Initialize metrics
  atomic_store(&pm->batches_processed, 0);
  atomic_store(&pm->samples_processed, 0);
  atomic_store(&pm->total_latency_ns, 0);
  atomic_store(&pm->max_latency_ns, 0);
  atomic_store(&pm->min_latency_ns, 0);
  atomic_store(&pm->max_queue_depth, 0);
  atomic_store(&pm->current_queue_depth, 0);

  return Bp_EC_OK;
}

void passthrough_metrics_get_metrics(PassthroughMetrics_t* pm, size_t* batches,
                                     size_t* samples, uint64_t* avg_latency_ns,
                                     size_t* max_queue)
{
  if (batches) *batches = atomic_load(&pm->batches_processed);
  if (samples) *samples = atomic_load(&pm->samples_processed);
  if (avg_latency_ns) {
    size_t total_b = atomic_load(&pm->batches_processed);
    if (total_b > 0) {
      *avg_latency_ns = atomic_load(&pm->total_latency_ns) / total_b;
    } else {
      *avg_latency_ns = 0;
    }
  }
  if (max_queue) *max_queue = atomic_load(&pm->max_queue_depth);
}

// Variable Batch Producer Implementation
static void* variable_batch_producer_worker(void* arg)
{
  VariableBatchProducer_t* vbp = (VariableBatchProducer_t*) arg;
  BP_WORKER_ASSERT(&vbp->base, vbp->base.sinks[0] != NULL, Bp_EC_NO_SINK);
  BP_WORKER_ASSERT(&vbp->base, vbp->batch_sizes != NULL, Bp_EC_NULL_POINTER);
  BP_WORKER_ASSERT(&vbp->base, vbp->n_batch_sizes > 0, Bp_EC_INVALID_CONFIG);

  // Initialize timing
  vbp->next_batch_time_ns = get_time_ns();

  while (atomic_load(&vbp->base.running)) {
    // Get current batch size from array
    uint32_t current_batch_size = vbp->batch_sizes[vbp->current_batch_index];

    // Sanity check batch size
    size_t max_batch_size = bb_batch_size(vbp->base.sinks[0]);
    BP_WORKER_ASSERT(&vbp->base, current_batch_size <= max_batch_size,
                     Bp_EC_INVALID_CONFIG);
    BP_WORKER_ASSERT(&vbp->base, current_batch_size > 0, Bp_EC_INVALID_CONFIG);

    // Get output batch
    Batch_t* output = bb_get_head(vbp->base.sinks[0]);
    BP_WORKER_ASSERT(&vbp->base, output != NULL, Bp_EC_NULL_POINTER);
    BP_WORKER_ASSERT(&vbp->base, output->data != NULL, Bp_EC_NULL_POINTER);

    // Check data type
    BP_WORKER_ASSERT(&vbp->base, vbp->base.sinks[0]->dtype == DTYPE_FLOAT,
                     Bp_EC_TYPE_MISMATCH);

    // Generate data based on pattern - only fill current_batch_size samples
    float* data = (float*) output->data;
    for (uint32_t i = 0; i < current_batch_size; i++) {
      switch (vbp->pattern) {
        case PATTERN_SEQUENTIAL:
          data[i] = (float) (vbp->next_sequence++);
          break;
        case PATTERN_CONSTANT:
          data[i] = 1.0f;  // Default constant value
          break;
        case PATTERN_SINE:
          data[i] = sinf(vbp->sine_phase);
          vbp->sine_phase += 0.1f;  // Fixed phase increment
          if (vbp->sine_phase > 2.0f * M_PI) {
            vbp->sine_phase -= 2.0f * M_PI;
          }
          break;
        case PATTERN_RANDOM:
          data[i] = (float) rand() / RAND_MAX;
          break;
      }
    }

    // Set batch metadata - KEY: head is set to actual samples, not capacity!
    output->head = current_batch_size;
    output->t_ns = vbp->next_batch_time_ns;
    output->period_ns = vbp->sample_period_ns;
    output->ec = Bp_EC_OK;

    // Submit batch
    Bp_EC err = bb_submit(vbp->base.sinks[0], vbp->base.timeout_us);
    if (err == Bp_EC_FILTER_STOPPING) {
      break;
    }
    BP_WORKER_ASSERT(&vbp->base, err == Bp_EC_OK, err);

    // Update metrics
    atomic_fetch_add(&vbp->total_batches, 1);
    atomic_fetch_add(&vbp->total_samples, current_batch_size);

    // Update timing for next batch
    vbp->next_batch_time_ns += vbp->sample_period_ns * current_batch_size;

    // Move to next batch size
    vbp->current_batch_index++;
    if (vbp->current_batch_index >= vbp->n_batch_sizes) {
      if (vbp->cycle_batch_sizes) {
        vbp->current_batch_index = 0;
        atomic_fetch_add(&vbp->cycles_completed, 1);
      } else {
        // Send completion signal and exit
        Batch_t* completion = bb_get_head(vbp->base.sinks[0]);
        completion->ec = Bp_EC_COMPLETE;
        completion->head = 0;
        err = bb_submit(vbp->base.sinks[0], vbp->base.timeout_us);
        BP_WORKER_ASSERT(&vbp->base, err == Bp_EC_OK, err);
        break;
      }
    }

    // Simple rate limiting (optional)
    usleep(1000);  // 1ms between batches
  }

  return NULL;
}

Bp_EC variable_batch_producer_init(VariableBatchProducer_t* vbp,
                                   VariableBatchProducerConfig_t config)
{
  if (!vbp) return Bp_EC_NULL_POINTER;
  if (!config.batch_sizes || config.n_batch_sizes == 0) {
    return Bp_EC_INVALID_CONFIG;
  }

  // Check if already initialized
  if (vbp->base.filt_type != FILT_T_NDEF) {
    return Bp_EC_ALREADY_RUNNING;
  }

  // Build core config - no input buffer needed for source filter
  Core_filt_config_t core_config = {.name = config.name,
                                    .filt_type = FILT_T_MAP,
                                    .size = sizeof(VariableBatchProducer_t),
                                    .n_inputs = 0,  // Source filter
                                    .max_supported_sinks = 1,
                                    .timeout_us = config.timeout_us,
                                    .worker = variable_batch_producer_worker};

  // Initialize base filter
  Bp_EC err = filt_init(&vbp->base, core_config);
  if (err != Bp_EC_OK) return err;

  // Copy configuration
  vbp->batch_sizes = config.batch_sizes;  // Note: using provided array directly
  vbp->n_batch_sizes = config.n_batch_sizes;
  vbp->cycle_batch_sizes = config.cycle_batch_sizes;
  vbp->pattern = config.pattern;
  vbp->sample_period_ns = config.sample_period_ns;
  vbp->start_sequence = config.start_sequence;

  // Initialize runtime state
  vbp->current_batch_index = 0;
  vbp->next_sequence = config.start_sequence;
  vbp->sine_phase = 0.0f;
  vbp->next_batch_time_ns = 0;

  // Initialize metrics
  atomic_store(&vbp->total_batches, 0);
  atomic_store(&vbp->total_samples, 0);
  atomic_store(&vbp->cycles_completed, 0);

  return Bp_EC_OK;
}

void variable_batch_producer_get_metrics(VariableBatchProducer_t* vbp,
                                         size_t* batches, size_t* samples,
                                         size_t* cycles)
{
  if (batches) *batches = atomic_load(&vbp->total_batches);
  if (samples) *samples = atomic_load(&vbp->total_samples);
  if (cycles) *cycles = atomic_load(&vbp->cycles_completed);
}