/**
 * @file test_lifecycle_with_worker.c
 * @brief Lifecycle test with worker thread
 */

#include "common.h"

void test_lifecycle_with_worker(void)
{
  // Initialize filter
  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  SKIP_IF_NO_WORKER();

  // Start input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_start(g_fut->input_buffers[i]);
  }

  // For filters with inputs, connect producers to ALL inputs
  ControllableProducer_t** producers = NULL;
  size_t n_producers_allocated = 0;
  size_t n_producers_initialized = 0;

  if (g_fut->n_input_buffers > 0) {
    producers = calloc(g_fut->n_input_buffers, sizeof(ControllableProducer_t*));
    ASSERT_ALLOC(producers, "producer array");

    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      producers[i] = calloc(1, sizeof(ControllableProducer_t));
      if (!producers[i]) {
        // Clean up previously allocated producers
        for (int j = 0; j < i; j++) {
          free(producers[j]);
        }
        free(producers);
        ASSERT_ALLOC_ARRAY(producers[i], i, g_fut->n_input_buffers, "producer");
      }
      n_producers_allocated = i + 1;

      ControllableProducerConfig_t prod_config = {
          .name = "test_producer",
          .timeout_us = 1000000,
          .samples_per_second = 1000,
          .pattern = PATTERN_SEQUENTIAL,
          .constant_value = 0.0,
          .sine_frequency = 0.0,
          .max_batches = 5,
          .burst_mode = false,
          .burst_on_batches = 0,
          .burst_off_batches = 0,
          .start_sequence = i * 1000  // Different sequences for each input
      };

      ASSERT_BP_OK_CTX(controllable_producer_init(producers[i], prod_config),
                       "Failed to init producer[%d]", i);
      n_producers_initialized = i + 1;

      ASSERT_PRODUCER_CONNECT(i, producers[i], g_fut, i);
    }
  }

  // For filters with outputs, connect a consumer
  ControllableConsumer_t* consumer = NULL;
  if (g_fut->max_supported_sinks > 0) {
    consumer =
        (ControllableConsumer_t*) calloc(1, sizeof(ControllableConsumer_t));
    ASSERT_ALLOC(consumer, "consumer");

    ControllableConsumerConfig_t consumer_config = {
        .name = "test_consumer",
        .buff_config = {.dtype = DTYPE_FLOAT,
                        .batch_capacity_expo = 6,
                        .ring_capacity_expo = 8,
                        .overflow_behaviour = OVERFLOW_BLOCK},
        .timeout_us = 1000000,
        .process_delay_us = 0,
        .validate_sequence = false,
        .validate_timing = false,
        .consume_pattern = 0,
        .slow_start = false,
        .slow_start_batches = 0};

    ASSERT_BP_OK(controllable_consumer_init(consumer, consumer_config));
    ASSERT_CONNECT_OK(g_fut, 0, consumer->base.input_buffers[0]);
  }

  // Start all producers
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      ASSERT_START_OK(&producers[i]->base);
    }
  }

  // Start filter
  ASSERT_START_OK(g_fut);
  TEST_ASSERT_TRUE_MESSAGE(atomic_load(&g_fut->running),
                           "Filter not running after start");

  // Start consumer if connected
  if (consumer) {
    ASSERT_START_OK(&consumer->base);
  }

  // Let it run briefly
  usleep(1000);  // 10ms

  // Stop all producers first
  if (producers) {
    for (int i = 0; i < g_fut->n_input_buffers; i++) {
      ASSERT_STOP_OK(&producers[i]->base);
    }
  }

  // Stop consumer
  if (consumer) {
    ASSERT_STOP_OK(&consumer->base);
  }

  // Stop filter
  ASSERT_STOP_OK(g_fut);
  TEST_ASSERT_FALSE_MESSAGE(atomic_load(&g_fut->running),
                            "Filter still running after stop");

  // Check for worker errors
  ASSERT_WORKER_OK(g_fut);

  // Cleanup producers - only deinit initialized ones
  if (producers) {
    for (int i = 0; i < n_producers_initialized; i++) {
      ASSERT_DEINIT_OK(&producers[i]->base);
    }
    for (int i = 0; i < n_producers_allocated; i++) {
      free(producers[i]);
    }
    free(producers);
  }

  // Cleanup consumer
  if (consumer) {
    ASSERT_DEINIT_OK(&consumer->base);
    free(consumer);
  }

  // Stop input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_stop(g_fut->input_buffers[i]);
  }

  // Deinit filter
  ASSERT_DEINIT_OK(g_fut);
}