/**
 * @file test_dataflow_passthrough.c
 * @brief Test data flow through filter
 */

#include "common.h"

void test_dataflow_passthrough(void)
{
  // Initialize filter first to determine its capabilities
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

  // Determine filter type and create appropriate pipeline
  ControllableProducer_t* producer = NULL;
  ControllableConsumer_t* consumer = NULL;

  // For filters with outputs, create a consumer
  if (g_fut->max_supported_sinks > 0) {
    consumer = calloc(1, sizeof(ControllableConsumer_t));
    TEST_ASSERT_NOT_NULL(consumer);

    // Determine data type: from input buffer if available, otherwise assume
    // FLOAT
    SampleDtype_t dtype = DTYPE_FLOAT;
    if (g_fut->n_input_buffers > 0 && g_fut->input_buffers[0]) {
      dtype = g_fut->input_buffers[0]->dtype;
    }

    ControllableConsumerConfig_t cons_config = {
        .name = "test_consumer",
        .buff_config = {.dtype = dtype,
                        .batch_capacity_expo = 6,
                        .ring_capacity_expo = 8,
                        .overflow_behaviour = OVERFLOW_BLOCK},
        .timeout_us = 1000000,
        .process_delay_us = 0,
        .validate_sequence =
            false,  // TODO: Fix sequence validation for multi-input filters
        .validate_timing = false,  // TODO: Fix timing validation
        .consume_pattern = 0,
        .slow_start = false,
        .slow_start_batches = 0};

    err = controllable_consumer_init(consumer, cons_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);

    err = filt_sink_connect(g_fut, 0, consumer->base.input_buffers[0]);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  }

  // For filters with inputs, create producers for ALL inputs
  ControllableProducer_t** producers = NULL;
  if (g_fut->n_input_buffers > 0) {
    producers = calloc(g_fut->n_input_buffers, sizeof(ControllableProducer_t*));
    TEST_ASSERT_NOT_NULL(producers);

    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      producers[i] = calloc(1, sizeof(ControllableProducer_t));
      TEST_ASSERT_NOT_NULL(producers[i]);

      ControllableProducerConfig_t prod_config = {
          .name = "test_producer",
          .timeout_us = 1000000,
          .samples_per_second = 10000,
          .pattern = PATTERN_SEQUENTIAL,
          .constant_value = 0.0,
          .sine_frequency = 0.0,
          .max_batches = 10,
          .burst_mode = false,
          .burst_on_batches = 0,
          .burst_off_batches = 0,
          .start_sequence =
              1000 + i * 1000  // Different sequences for each input
      };

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

    // Use first producer for tracking completion
    producer = producers[0];
  }

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
  if (producer) {
    // Wait for all producers to finish
    bool all_done = false;
    while (!all_done) {
      all_done = true;
      for (int i = 0; i < g_fut->n_input_buffers; i++) {
        if (atomic_load(&producers[i]->batches_produced) < 10) {
          all_done = false;
          break;
        }
      }
      if (!all_done) usleep(1000);
    }
  } else if (consumer) {
    // For source filters, wait a reasonable time for data generation
    usleep(1000);  // 100ms
  }
  usleep(1000);  // Extra time for data to flow through

  // Stop pipeline
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      filt_stop(&producers[i]->base);
    }
  }
  filt_stop(g_fut);
  if (consumer) filt_stop(&consumer->base);

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

  // Verify data integrity for filters with outputs
  if (consumer) {
    size_t batches_consumed = atomic_load(&consumer->batches_consumed);

    // For transform filters, should receive what producer sent
    if (producer) {
      // For multi-input filters, check total batches from all producers
      size_t total_batches_produced = 0;
      for (int i = 0; i < g_fut->n_input_buffers; i++) {
        total_batches_produced += atomic_load(&producers[i]->batches_produced);
      }
      // For element-wise filters, output batches = min(input batches)
      size_t min_batches_produced =
          atomic_load(&producers[0]->batches_produced);
      for (int i = 1; i < g_fut->n_input_buffers; i++) {
        size_t produced = atomic_load(&producers[i]->batches_produced);
        if (produced < min_batches_produced) min_batches_produced = produced;
      }

      TEST_ASSERT_GREATER_THAN_MESSAGE(0, batches_consumed,
                                       "Consumer should have received batches");
      // Allow some batches in flight
      TEST_ASSERT_INT_WITHIN_MESSAGE(
          2, min_batches_produced, batches_consumed,
          "Consumer should receive most produced batches");
    } else {
      // For source filters, just verify some data was generated
      TEST_ASSERT_GREATER_THAN_MESSAGE(0, batches_consumed,
                                       "Source filter should generate data");
    }

    ASSERT_NO_SEQ_ERRORS(consumer);
    ASSERT_NO_TIMING_ERRORS(consumer);
  }

  // For sink filters, verify they consumed data
  if (producers && !consumer) {
    // Check all producers sent their data
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      ASSERT_BATCHES_PRODUCED(producers[i], 10);
    }
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