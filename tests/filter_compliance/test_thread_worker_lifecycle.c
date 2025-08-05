/**
 * @file test_thread_worker_lifecycle.c
 * @brief Test worker thread lifecycle
 */

#include "common.h"

void test_thread_worker_lifecycle(void)
{
  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  SKIP_IF_NO_WORKER();

  // Verify worker thread not running
  TEST_ASSERT_FALSE(atomic_load(&g_fut->running));

  // Start input buffers if any
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_start(g_fut->input_buffers[i]);
  }

  // For filters with outputs, connect a dummy consumer to avoid NO_SINK errors
  ControllableConsumer_t* consumer = NULL;
  if (g_fut->max_supported_sinks > 0) {
    consumer =
        (ControllableConsumer_t*) calloc(1, sizeof(ControllableConsumer_t));
    ASSERT_ALLOC(consumer, "consumer");

    ControllableConsumerConfig_t consumer_config = {
        .name = "lifecycle_test_consumer",
        .buff_config = {.dtype = (g_fut->n_input_buffers > 0 &&
                                  g_fut->input_buffers[0])
                                     ? g_fut->input_buffers[0]->dtype
                                     : DTYPE_FLOAT,
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
    ASSERT_START_OK(&consumer->base);
  }

  // Start should create worker thread
  err = filt_start(g_fut);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  TEST_ASSERT_TRUE(atomic_load(&g_fut->running));

  // Give thread time to actually start
  usleep(1000);

  // Stop should terminate worker thread
  err = filt_stop(g_fut);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);
  TEST_ASSERT_FALSE(atomic_load(&g_fut->running));

  // Verify clean termination
  TEST_ASSERT_EQUAL(Bp_EC_OK, g_fut->worker_err_info.ec);

  // Stop and cleanup consumer if connected
  if (consumer) {
    filt_stop(&consumer->base);
    filt_deinit(&consumer->base);
    free(consumer);
  }

  // Stop input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_stop(g_fut->input_buffers[i]);
  }

  filt_deinit(g_fut);
}