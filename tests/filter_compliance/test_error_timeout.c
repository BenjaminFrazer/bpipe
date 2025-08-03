/**
 * @file test_error_timeout.c
 * @brief Test timeout handling
 */

#include "common.h"

void test_error_timeout(void)
{
  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  SKIP_IF_NO_INPUTS();
  SKIP_IF_NO_WORKER();

  // Start input buffers
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
        .name = "timeout_test_consumer",
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

  // Start filter with no producer - should handle timeouts gracefully
  err = filt_start(g_fut);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // Let it run and timeout a few times
  usleep(50000);  // 50ms

  // Stop should work cleanly even with timeouts
  err = filt_stop(g_fut);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // No worker errors expected from timeouts
  TEST_ASSERT_EQUAL_MESSAGE(Bp_EC_OK, g_fut->worker_err_info.ec,
                            "Timeout caused worker error");

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