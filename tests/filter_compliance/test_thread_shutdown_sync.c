/**
 * @file test_thread_shutdown_sync.c
 * @brief Test synchronized shutdown with blocked buffers
 * 
 * This test verifies that filters can be cleanly shut down even when their
 * output buffers are blocked. It tests the force_return mechanism that allows
 * filters to exit from blocking buffer operations during shutdown.
 */

#include "common.h"

void test_thread_shutdown_sync(void)
{

  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  SKIP_IF_NO_OUTPUTS();

  // Start input buffers if any
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_start(g_fut->input_buffers[i]);
  }

  // Create a consumer that will block
  ControllableConsumer_t consumer = {0};  // Zero-initialize

  // For passthrough filter, use matching batch sizes to avoid memory corruption
  uint8_t consumer_batch_expo = 2;  // Default very small buffer
  if (g_fut->filt_type == FILT_T_MATCHED_PASSTHROUGH) {  // Passthrough filter
    // Match the input buffer batch size
    consumer_batch_expo = g_fut->input_buffers[0]->batch_capacity_expo;
  }

  ControllableConsumerConfig_t cons_config = {
      .name = "blocking_consumer",
      .buff_config = {.dtype = (g_fut->n_input_buffers > 0 &&
                                g_fut->input_buffers[0])
                                   ? g_fut->input_buffers[0]->dtype
                                   : DTYPE_FLOAT,
                      .batch_capacity_expo = consumer_batch_expo,
                      .ring_capacity_expo = 2,  // Very small ring
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 10000000,       // 10 second timeout
      .process_delay_us = 1000000,  // 1 second per batch - extremely slow
      .validate_sequence = false,
      .validate_timing = false,
      .consume_pattern = 0,
      .slow_start = false,
      .slow_start_batches = 0};

  err = controllable_consumer_init(&consumer, cons_config);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // Connect
  err = filt_sink_connect(g_fut, 0, consumer.base.input_buffers[0]);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // Start both
  err = filt_start(g_fut);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  err = filt_start(&consumer.base);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // Let them run briefly
  usleep(1000);  // 10ms

  // Now stop - this tests force_return mechanism
  err = filt_stop(&consumer.base);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  err = filt_stop(g_fut);
  TEST_ASSERT_EQUAL(Bp_EC_OK, err);

  // Both should have stopped cleanly
  TEST_ASSERT_FALSE(atomic_load(&g_fut->running));
  TEST_ASSERT_FALSE(atomic_load(&consumer.base.running));

  // Cleanup
  filt_deinit(&consumer.base);
  filt_deinit(g_fut);
}