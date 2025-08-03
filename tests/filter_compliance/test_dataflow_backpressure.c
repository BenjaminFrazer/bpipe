/**
 * @file test_dataflow_backpressure.c
 * @brief Test backpressure handling
 */

#include "common.h"

void test_dataflow_backpressure(void)
{
  // Initialize filter first to determine its capabilities
  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // This test specifically needs both inputs and outputs to test backpressure
  // propagation However, we can test partial backpressure for source/sink
  // filters
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

  // For filters with outputs, create a slow consumer
  if (g_fut->max_supported_sinks > 0) {
    consumer = calloc(1, sizeof(ControllableConsumer_t));
    TEST_ASSERT_NOT_NULL(consumer);

    SampleDtype_t dtype = DTYPE_FLOAT;
    if (g_fut->n_input_buffers > 0 && g_fut->input_buffers[0]) {
      dtype = g_fut->input_buffers[0]->dtype;
    }

    // For passthrough, use matching batch size to avoid buffer overflow
    uint8_t consumer_batch_expo =
        (g_fut->filt_type == FILT_T_MATCHED_PASSTHROUGH &&
         g_fut->n_input_buffers > 0 && g_fut->input_buffers[0])
            ? g_fut->input_buffers[0]->batch_capacity_expo
            : 4;  // Default small buffer for other filters

    ControllableConsumerConfig_t cons_config = {
        .name = "slow_consumer",
        .buff_config = {.dtype = dtype,
                        .batch_capacity_expo = consumer_batch_expo,
                        .ring_capacity_expo = 4,  // Small ring
                        .overflow_behaviour = OVERFLOW_BLOCK},
        .timeout_us = 1000000,
        .process_delay_us = 50000,  // 50ms per batch - extremely slow
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

  // For filters with inputs, create fast producers for ALL inputs
  ControllableProducer_t** producers = NULL;
  if (g_fut->n_input_buffers > 0) {
    producers = calloc(g_fut->n_input_buffers, sizeof(ControllableProducer_t*));
    TEST_ASSERT_NOT_NULL(producers);

    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      producers[i] = calloc(1, sizeof(ControllableProducer_t));
      TEST_ASSERT_NOT_NULL(producers[i]);

      ControllableProducerConfig_t prod_config = {
          .name = "fast_producer",
          .timeout_us = 1000000,
          .samples_per_second = 1000000,  // 1M samples/sec
          .pattern = PATTERN_CONSTANT,
          .constant_value = 42.0 + i,  // Different values for each input
          .sine_frequency = 0.0,
          .max_batches =
              200,  // Increase to ensure backpressure has time to kick in
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

  // Start pipeline
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

  // Let it run for a bit
  usleep(500000);  // 500ms to give backpressure time to work

  // Stop pipeline
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      err = filt_stop(&producers[i]->base);
      TEST_ASSERT_EQUAL(Bp_EC_OK, err);
    }
  }
  err = filt_stop(g_fut);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  if (consumer) {
    err = filt_stop(&consumer->base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  }

  // Verify backpressure behavior based on filter type
  if (producer && consumer) {
    // Full pipeline: verify no data loss
    // For multi-input filters, check the minimum produced (element-wise)
    size_t min_produced = atomic_load(&producers[0]->batches_produced);
    for (int i = 1; i < g_fut->n_input_buffers; i++) {
      size_t produced = atomic_load(&producers[i]->batches_produced);
      if (produced < min_produced) min_produced = produced;
    }
    size_t consumed = atomic_load(&consumer->batches_consumed);

    // Producers should have been slowed down by backpressure
    TEST_ASSERT_LESS_THAN_MESSAGE(
        200, min_produced, "Producers should be throttled by backpressure");

    // Allow for batches in flight (passthrough + consumer buffers)
    // Total buffering capacity is passthrough ring (32) + consumer ring (16) =
    // 48
    TEST_ASSERT_INT_WITHIN_MESSAGE(48, min_produced, consumed,
                                   "Backpressure caused data loss");
  } else if (producer && !consumer) {
    // Sink filter: should consume at its own rate
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      size_t produced = atomic_load(&producers[i]->batches_produced);
      TEST_ASSERT_GREATER_THAN_MESSAGE(
          0, produced, "Producer should have sent data to sink");
    }
  } else if (!producer && consumer) {
    // Source filter: backpressure should slow down generation
    size_t consumed = atomic_load(&consumer->batches_consumed);

    // With 10ms per batch and 100ms runtime, should consume ~10 batches
    TEST_ASSERT_LESS_THAN_MESSAGE(
        20, consumed, "Source should be throttled by slow consumer");
    TEST_ASSERT_GREATER_THAN_MESSAGE(
        0, consumed, "Consumer should have received some batches");
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