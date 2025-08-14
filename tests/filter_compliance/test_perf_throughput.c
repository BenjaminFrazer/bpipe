/**
 * @file test_perf_throughput.c
 * @brief Test filter throughput performance
 */

#include "common.h"

void test_perf_throughput(void)
{
  // Apply performance buffer profile if the filter has buffer configuration
  FilterRegistration_t* reg = &g_filters[g_current_filter];
  if (reg->has_buff_config) {
    apply_buffer_profile(g_fut_config, reg->buff_config_offset, 
                        BUFF_PROFILE_PERF);
  }
  
  // Initialize filter first to check capabilities
  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  
  // Skip if filter has neither inputs nor outputs
  if (g_fut->n_input_buffers == 0 && g_fut->max_supported_sinks == 0) {
    TEST_IGNORE_MESSAGE("Filter has neither inputs nor outputs");
    return;
  }

  // Start input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_start(g_fut->input_buffers[i]);
  }

  ControllableProducer_t* producer = NULL;
  ControllableConsumer_t* consumer = NULL;
  size_t target_batches = 1000;

  // For filters with outputs, create a fast consumer
  if (g_fut->max_supported_sinks > 0) {
    consumer = calloc(1, sizeof(ControllableConsumer_t));
    TEST_ASSERT_NOT_NULL(consumer);

    SampleDtype_t dtype = DTYPE_FLOAT;
    if (g_fut->n_input_buffers > 0 && g_fut->input_buffers[0]) {
      dtype = g_fut->input_buffers[0]->dtype;
    }

    ControllableConsumerConfig_t cons_config = {
        .name = "perf_consumer",
        .buff_config = {.dtype = dtype,
                        .batch_capacity_expo = 10,  // Large batches
                        .ring_capacity_expo = 8,    // Large ring
                        .overflow_behaviour = OVERFLOW_BLOCK},
        .timeout_us = 1000000,
        .process_delay_us = 0,  // No artificial delay
        .validate_sequence = false,
        .validate_timing = false,
        .consume_pattern = 0,
        .slow_start = false,
        .slow_start_batches = 0};

    err = controllable_consumer_init(consumer, cons_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);

    err = filt_sink_connect(g_fut, 0, consumer->base.input_buffers[0]);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  }

  // For filters with inputs, create high-rate producers for ALL inputs
  ControllableProducer_t** producers = NULL;
  if (g_fut->n_input_buffers > 0) {
    producers = calloc(g_fut->n_input_buffers, sizeof(ControllableProducer_t*));
    TEST_ASSERT_NOT_NULL(producers);

    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      producers[i] = calloc(1, sizeof(ControllableProducer_t));
      TEST_ASSERT_NOT_NULL(producers[i]);

      ControllableProducerConfig_t prod_config = {
          .name = "perf_producer",
          .timeout_us = 1000000,
          .samples_per_second = 10000000,  // 10M samples/sec target
          .pattern = PATTERN_CONSTANT,
          .constant_value = 1.0 + i * 0.1,  // Slightly different values
          .sine_frequency = 0.0,
          .max_batches = target_batches,
          .burst_mode = false,
          .burst_on_batches = 0,
          .burst_off_batches = 0,
          .start_sequence = 0};

      err = controllable_producer_init(producers[i], prod_config);
      if (err != Bp_EC_OK) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to init producer[%d]: %s", i,
                 err_lut[err]);
        TEST_FAIL_MESSAGE(msg);
      }

      err = filt_sink_connect(&producers[i]->base, 0, g_fut->input_buffers[i]);
      if (err != Bp_EC_OK) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "Failed to connect producer[%d] to %s input[%d]: %s", i,
                 g_fut->name, i, err_lut[err]);
        TEST_FAIL_MESSAGE(msg);
      }
    }

    // Use first producer for tracking
    producer = producers[0];
  }

  // Measure time
  uint64_t start_ns = get_time_ns();

  // Start all components
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      err = filt_start(&producers[i]->base);
      TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    }
  }

  err = filt_start(g_fut);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  if (consumer) {
    err = filt_start(&consumer->base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  }

  // Wait for completion based on filter type
  if (producer && consumer) {
    // Transform filter: wait for consumer to receive all data
    // For element-wise filters, output = min(inputs)
    while (atomic_load(&consumer->batches_consumed) < target_batches - 5) {
      usleep(1000);
    }
  } else if (producer && !consumer) {
    // Sink filter: wait for all producers to send all data
    bool all_done = false;
    while (!all_done) {
      all_done = true;
      for (int i = 0; i < g_fut->n_input_buffers; i++) {
        if (atomic_load(&producers[i]->batches_produced) < target_batches) {
          all_done = false;
          break;
        }
      }
      if (!all_done) usleep(1000);
    }
    usleep(1000);  // Extra time for sink to process
  } else if (!producer && consumer) {
    // Source filter: run for fixed time
    usleep(1000);  // 500ms
  }

  uint64_t elapsed_ns = get_time_ns() - start_ns;

  // Stop pipeline
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      filt_stop(&producers[i]->base);
    }
  }
  filt_stop(g_fut);
  if (consumer) filt_stop(&consumer->base);

  // Calculate throughput based on filter type
  size_t total_samples = 0;
  size_t batches_processed = 0;
  double throughput = 0;

  if (consumer) {
    total_samples = atomic_load(&consumer->samples_consumed);
    batches_processed = atomic_load(&consumer->batches_consumed);
  } else if (producers) {
    // For sink filters, use total from all producers
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      size_t samples = atomic_load(&producers[i]->samples_generated);
      size_t batches = atomic_load(&producers[i]->batches_produced);
      total_samples += samples;
      batches_processed += batches;
    }
    // For multi-input element-wise, actual processed = min(inputs)
    if (g_fut->n_input_buffers > 1) {
      size_t min_samples = atomic_load(&producers[0]->samples_generated);
      size_t min_batches = atomic_load(&producers[0]->batches_produced);
      for (int i = 1; i < g_fut->n_input_buffers; i++) {
        size_t samples = atomic_load(&producers[i]->samples_generated);
        size_t batches = atomic_load(&producers[i]->batches_produced);
        if (samples < min_samples) min_samples = samples;
        if (batches < min_batches) min_batches = batches;
      }
      batches_processed = min_batches;
      total_samples = min_samples;
    }
  }

  if (total_samples > 0) {
    throughput = (double) total_samples * 1e9 / elapsed_ns;

    g_last_perf_metrics.throughput_samples_per_sec = throughput;
    g_last_perf_metrics.batches_processed = batches_processed;

    // Record in performance report
    char buf[256];
    snprintf(buf, sizeof(buf), "  Throughput: %.2f Msamples/sec\n",
             throughput / 1e6);
    strcat(g_perf_report, buf);
    snprintf(buf, sizeof(buf), "  Batches: %zu\n", batches_processed);
    strcat(g_perf_report, buf);
    snprintf(buf, sizeof(buf), "  Time: %.2f ms\n", elapsed_ns / 1e6);
    strcat(g_perf_report, buf);

    // Different thresholds for different filter types
    double min_throughput = 100000;  // 100K samples/sec for transform filters
    if (!producer || !consumer) {
      min_throughput = 50000;  // 50K samples/sec for source/sink filters
    }

    TEST_ASSERT_GREATER_THAN_MESSAGE(
        min_throughput, throughput,
        "Filter throughput below minimum threshold");
  }

  // Check for errors
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      TEST_ASSERT_EQUAL(Bp_EC_OK, producers[i]->base.worker_err_info.ec);
    }
  }
  TEST_ASSERT_EQUAL(Bp_EC_OK, g_fut->worker_err_info.ec);
  if (consumer) {
    TEST_ASSERT_EQUAL(Bp_EC_OK, consumer->base.worker_err_info.ec);
  }

  // Cleanup
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      filt_deinit(&producers[i]->base);
      free(producers[i]);
    }
    free(producers);
  }
  if (consumer) {
    filt_deinit(&consumer->base);
    free(consumer);
  }

  // Stop input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_stop(g_fut->input_buffers[i]);
  }

  // Note: g_fut is deinit'd in tearDown()
}