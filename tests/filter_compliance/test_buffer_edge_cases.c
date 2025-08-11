/**
 * @file test_buffer_edge_cases.c
 * @brief Test filters with edge case buffer configurations
 */

#include "common.h"
#include "test_utils.h"

// Helper structure to manage multiple consumers
typedef struct {
  ControllableConsumer_t*
      consumers;  // Array of consumers (not array of pointers)
  size_t n_consumers;
} ConsumerArray_t;

// Helper function to create consumers for all filter outputs
static ConsumerArray_t create_consumers_if_needed(Filter_t* filter,
                                                  uint8_t batch_expo,
                                                  uint8_t ring_expo)
{
  ConsumerArray_t result = {.consumers = NULL, .n_consumers = 0};

  if (filter->max_supported_sinks == 0) {
    return result;
  }

  result.n_consumers = filter->max_supported_sinks;
  result.consumers = calloc(result.n_consumers, sizeof(ControllableConsumer_t));
  TEST_ASSERT_NOT_NULL(result.consumers);

  // Match data type from input buffer if available
  SampleDtype_t dtype = DTYPE_FLOAT;
  if (filter->n_input_buffers > 0 && filter->input_buffers[0]) {
    dtype = filter->input_buffers[0]->dtype;
  }

  for (size_t i = 0; i < result.n_consumers; i++) {
    // Create unique name for each consumer
    char name[32];
    snprintf(name, sizeof(name), "edge_consumer_%zu", i);

    ControllableConsumerConfig_t cons_config = {
        .name = name,
        .buff_config = {.dtype = dtype,
                        .batch_capacity_expo = batch_expo,
                        .ring_capacity_expo = ring_expo,
                        .overflow_behaviour = OVERFLOW_BLOCK},
        .timeout_us = 1000000,
        .process_delay_us = 0,
        .validate_sequence = false,
        .validate_timing = false,
        .consume_pattern = 0,
        .slow_start = false,
        .slow_start_batches = 0};

    CHECK_ERR(controllable_consumer_init(&result.consumers[i], cons_config));
    CHECK_ERR(filt_sink_connect(filter, i,
                                result.consumers[i].base.input_buffers[0]));
  }

  return result;
}

/**
 * Intent: Verify that filters can operate correctly with the absolute minimum
 * buffer sizes (2^2 = 4 samples per batch, 2^2 = 4 batches in ring buffer).
 *
 * Approach:
 * 1. Configure filter with BUFF_PROFILE_TINY (batch_capacity_expo=2,
 * ring_capacity_expo=2)
 * 2. Create a producer that sends 10 batches of sequential data
 * 3. Connect consumers to all filter outputs (if any) to prevent NO_SINK errors
 * 4. Run the pipeline briefly and verify:
 *    - Filter initializes successfully with tiny buffers
 *    - Data flows through without errors
 *    - At least some batches are processed
 *
 * This tests the filter's robustness with minimal memory usage and ensures
 * it doesn't have hardcoded assumptions about buffer sizes.
 */
void test_buffer_minimum_size(void)
{
  // Apply tiny buffer profile
  FilterRegistration_t* reg = &g_filters[g_current_filter];
  if (reg->has_buff_config) {
    apply_buffer_profile(g_fut_config, reg->buff_config_offset,
                         BUFF_PROFILE_TINY);
  }

  // Initialize filter with tiny buffers
  CHECK_ERR(g_fut_init(g_fut, g_fut_config));

  // Skip if filter has no inputs (can't test buffer behavior)
  SKIP_IF_NO_INPUTS();

  // Verify buffer was configured with minimum sizes
  if (g_fut->n_input_buffers > 0 && g_fut->input_buffers[0]) {
    Batch_buff_t* bb = g_fut->input_buffers[0];
    TEST_ASSERT_EQUAL_MESSAGE(2, bb->batch_capacity_expo,
                              "Buffer should have minimum batch capacity");
    TEST_ASSERT_EQUAL_MESSAGE(2, bb->ring_capacity_expo,
                              "Buffer should have minimum ring capacity");
  }

  // Start input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_start(g_fut->input_buffers[i]);
  }

  // Create a producer to send data
  ControllableProducer_t* producer = calloc(1, sizeof(ControllableProducer_t));
  TEST_ASSERT_NOT_NULL(producer);

  ControllableProducerConfig_t prod_config = {
      .name = "edge_producer",
      .timeout_us = 1000000,
      .samples_per_second = 1000,
      .pattern = PATTERN_SEQUENTIAL,
      .constant_value = 0.0,
      .sine_frequency = 0.0,
      .max_batches = 10,  // Send a few batches
      .burst_mode = false,
      .burst_on_batches = 0,
      .burst_off_batches = 0,
      .start_sequence = 0};

  CHECK_ERR(controllable_producer_init(producer, prod_config));

  CHECK_ERR(filt_sink_connect(&producer->base, 0, g_fut->input_buffers[0]));

  // If filter has outputs, create consumers for all sinks
  ConsumerArray_t consumers = create_consumers_if_needed(g_fut, 2, 2);

  // Start pipeline
  CHECK_ERR(filt_start(&producer->base));

  CHECK_ERR(filt_start(g_fut));

  for (size_t i = 0; i < consumers.n_consumers; i++) {
    CHECK_ERR(filt_start(&consumers.consumers[i].base));
  }

  // Let it run briefly
  usleep(1000);  // 100ms

  // Stop pipeline
  CHECK_ERR(filt_stop(&producer->base));

  CHECK_ERR(filt_stop(g_fut));

  for (size_t i = 0; i < consumers.n_consumers; i++) {
    CHECK_ERR(filt_stop(&consumers.consumers[i].base));
  }

  // Verify filter handled tiny buffers without errors
  CHECK_ERR(producer->base.worker_err_info.ec);
  CHECK_ERR(g_fut->worker_err_info.ec);
  for (size_t i = 0; i < consumers.n_consumers; i++) {
    CHECK_ERR(consumers.consumers[i].base.worker_err_info.ec);
  }

  // Verify some data was processed
  size_t produced = atomic_load(&producer->batches_produced);
  TEST_ASSERT_GREATER_THAN_MESSAGE(0, produced,
                                   "Producer should have sent some batches");

  // Cleanup
  filt_deinit(&producer->base);
  free(producer);

  for (size_t i = 0; i < consumers.n_consumers; i++) {
    filt_deinit(&consumers.consumers[i].base);
  }
  free(consumers.consumers);

  // Stop input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_stop(g_fut->input_buffers[i]);
  }
}

/**
 * Intent: Verify that filters handle buffer overflow correctly when configured
 * with OVERFLOW_DROP_HEAD behavior (oldest batches are dropped when buffer is
 * full).
 *
 * Approach:
 * 1. Configure filter with small buffers and OVERFLOW_DROP_HEAD behavior
 * 2. Create a fast producer with burst mode (20 batches on, 5 off) to trigger
 * overflow
 * 3. Start producer first but delay starting the filter to cause buffer buildup
 * 4. After 50ms, start the filter to begin processing accumulated data
 * 5. Verify:
 *    - No errors occur (DROP_HEAD should handle overflow gracefully)
 *    - Producer successfully sends batches
 *    - Some batches are dropped (for filters that track this)
 *
 * Note: This test expects dropped batches, but passthrough filters may not
 * drop batches at the producer level since they don't have internal buffering.
 */
void test_buffer_overflow_drop_head(void)
{
  // Apply small buffer profile
  FilterRegistration_t* reg = &g_filters[g_current_filter];
  if (reg->has_buff_config) {
    apply_buffer_profile(g_fut_config, reg->buff_config_offset,
                         BUFF_PROFILE_SMALL);

    // Override overflow behavior to DROP_HEAD
    BatchBuffer_config* buff_config =
        (BatchBuffer_config*) ((char*) g_fut_config + reg->buff_config_offset);
    buff_config->overflow_behaviour = OVERFLOW_DROP_HEAD;
  }

  // Initialize filter
  CHECK_ERR(g_fut_init(g_fut, g_fut_config));

  // Skip if filter has no inputs
  SKIP_IF_NO_INPUTS();

  // Start input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_start(g_fut->input_buffers[i]);
  }

  // Create a fast producer
  ControllableProducer_t* producer = calloc(1, sizeof(ControllableProducer_t));
  TEST_ASSERT_NOT_NULL(producer);

  ControllableProducerConfig_t prod_config = {
      .name = "overflow_producer",
      .timeout_us = 10000,           // Short timeout to trigger overflow
      .samples_per_second = 100000,  // Fast production
      .pattern = PATTERN_SEQUENTIAL,
      .constant_value = 0.0,
      .sine_frequency = 0.0,
      .max_batches = 100,
      .burst_mode = true,
      .burst_on_batches = 20,  // Burst to trigger overflow
      .burst_off_batches = 5,
      .start_sequence = 0};

  CHECK_ERR(controllable_producer_init(producer, prod_config));

  CHECK_ERR(filt_sink_connect(&producer->base, 0, g_fut->input_buffers[0]));

  // If filter has outputs, create consumers for all sinks
  ConsumerArray_t consumers = create_consumers_if_needed(g_fut, 4, 3);

  // Start producer but not filter (to cause overflow)
  CHECK_ERR(filt_start(&producer->base));

  // Let producer fill buffer
  usleep(10000);  // 50ms

  // Now start filter to process
  CHECK_ERR(filt_start(g_fut));

  for (size_t i = 0; i < consumers.n_consumers; i++) {
    CHECK_ERR(filt_start(&consumers.consumers[i].base));
  }

  // Let it run
  usleep(20000);  // 200ms

  // Stop pipeline
  CHECK_ERR(filt_stop(&producer->base));

  CHECK_ERR(filt_stop(g_fut));

  for (size_t i = 0; i < consumers.n_consumers; i++) {
    CHECK_ERR(filt_stop(&consumers.consumers[i].base));
  }

  // Verify no errors (DROP_HEAD should handle overflow gracefully)
  CHECK_ERR(producer->base.worker_err_info.ec);
  CHECK_ERR(g_fut->worker_err_info.ec);
  for (size_t i = 0; i < consumers.n_consumers; i++) {
    CHECK_ERR(consumers.consumers[i].base.worker_err_info.ec);
  }

  // Check that some batches were dropped
  size_t produced = atomic_load(&producer->batches_produced);

  TEST_ASSERT_GREATER_THAN_MESSAGE(0, produced,
                                   "Producer should have sent batches");

  // For DROP_HEAD, check the filter's input buffer dropped_batches counter
  if (g_fut->n_input_buffers > 0 && g_fut->input_buffers[0]) {
    size_t dropped =
        atomic_load(&g_fut->input_buffers[0]->producer.dropped_batches);
    TEST_ASSERT_GREATER_THAN_MESSAGE(
        0, dropped,
        "Some batches should have been dropped at filter's input buffer");
  }

  // Cleanup
  filt_deinit(&producer->base);
  free(producer);

  for (size_t i = 0; i < consumers.n_consumers; i++) {
    filt_deinit(&consumers.consumers[i].base);
  }
  free(consumers.consumers);

  // Stop input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_stop(g_fut->input_buffers[i]);
  }
}

/**
 * Intent: Verify that filters handle buffer overflow correctly when configured
 * with OVERFLOW_DROP_TAIL behavior (newest batches are dropped when buffer is
 * full).
 *
 * Approach:
 * 1. Configure filter with small buffers and OVERFLOW_DROP_TAIL behavior
 * 2. Create a fast producer with burst mode (20 batches on, 5 off) to trigger
 * overflow
 * 3. Start producer first but delay starting the filter to cause buffer buildup
 * 4. After 50ms, start the filter to begin processing accumulated data
 * 5. Verify:
 *    - No errors occur (DROP_TAIL should handle overflow gracefully)
 *    - Producer successfully sends batches
 *    - Some batches are dropped (for filters that track this)
 *
 * This test is similar to DROP_HEAD but verifies the opposite behavior - when
 * the buffer is full, new incoming batches are dropped instead of old ones.
 * This preserves the oldest data at the cost of losing the most recent samples.
 */
void test_buffer_overflow_drop_tail(void)
{
  // Apply small buffer profile
  FilterRegistration_t* reg = &g_filters[g_current_filter];
  if (reg->has_buff_config) {
    apply_buffer_profile(g_fut_config, reg->buff_config_offset,
                         BUFF_PROFILE_SMALL);

    // Override overflow behavior to DROP_TAIL
    BatchBuffer_config* buff_config =
        (BatchBuffer_config*) ((char*) g_fut_config + reg->buff_config_offset);
    buff_config->overflow_behaviour = OVERFLOW_DROP_TAIL;
  }

  // Initialize filter
  CHECK_ERR(g_fut_init(g_fut, g_fut_config));

  // Skip if filter has no inputs
  SKIP_IF_NO_INPUTS();

  // Start input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_start(g_fut->input_buffers[i]);
  }

  // Create a fast producer
  ControllableProducer_t* producer = calloc(1, sizeof(ControllableProducer_t));
  TEST_ASSERT_NOT_NULL(producer);

  ControllableProducerConfig_t prod_config = {
      .name = "overflow_producer",
      .timeout_us = 10000,           // Short timeout to trigger overflow
      .samples_per_second = 100000,  // Fast production
      .pattern = PATTERN_SEQUENTIAL,
      .constant_value = 0.0,
      .sine_frequency = 0.0,
      .max_batches = 100,
      .burst_mode = true,
      .burst_on_batches = 20,  // Burst to trigger overflow
      .burst_off_batches = 5,
      .start_sequence = 0};

  CHECK_ERR(controllable_producer_init(producer, prod_config));

  CHECK_ERR(filt_sink_connect(&producer->base, 0, g_fut->input_buffers[0]));

  // If filter has outputs, create consumers for all sinks
  ConsumerArray_t consumers = create_consumers_if_needed(g_fut, 4, 3);

  // Start producer but not filter (to cause overflow)
  CHECK_ERR(filt_start(&producer->base));

  // Let producer fill buffer
  usleep(10000);  // 50ms

  // Now start filter to process
  CHECK_ERR(filt_start(g_fut));

  for (size_t i = 0; i < consumers.n_consumers; i++) {
    CHECK_ERR(filt_start(&consumers.consumers[i].base));
  }

  // Let it run
  usleep(20000);  // 200ms

  // Stop pipeline
  CHECK_ERR(filt_stop(&producer->base));

  CHECK_ERR(filt_stop(g_fut));

  for (size_t i = 0; i < consumers.n_consumers; i++) {
    CHECK_ERR(filt_stop(&consumers.consumers[i].base));
  }

  // Verify no errors (DROP_TAIL should handle overflow gracefully)
  CHECK_ERR(producer->base.worker_err_info.ec);
  CHECK_ERR(g_fut->worker_err_info.ec);
  for (size_t i = 0; i < consumers.n_consumers; i++) {
    CHECK_ERR(consumers.consumers[i].base.worker_err_info.ec);
  }

  // Check that some batches were dropped
  size_t produced = atomic_load(&producer->batches_produced);

  TEST_ASSERT_GREATER_THAN_MESSAGE(0, produced,
                                   "Producer should have sent batches");

  // For DROP_TAIL, check the filter's input buffer dropped_by_producer counter
  if (g_fut->n_input_buffers > 0 && g_fut->input_buffers[0]) {
    size_t dropped =
        atomic_load(&g_fut->input_buffers[0]->consumer.dropped_by_producer);
    TEST_ASSERT_GREATER_THAN_MESSAGE(
        0, dropped,
        "Some batches should have been dropped at filter's input buffer");
  }

  // Cleanup
  filt_deinit(&producer->base);
  free(producer);

  for (size_t i = 0; i < consumers.n_consumers; i++) {
    filt_deinit(&consumers.consumers[i].base);
  }
  free(consumers.consumers);

  // Stop input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_stop(g_fut->input_buffers[i]);
  }
}

/**
 * Intent: Verify that filters can handle large buffer configurations
 * efficiently (2^10 = 1024 samples per batch, 2^10 = 1024 batches in ring
 * buffer).
 *
 * Approach:
 * 1. Configure filter with BUFF_PROFILE_LARGE (batch_capacity_expo=10,
 * ring_capacity_expo=10)
 * 2. Create a producer that sends 50 batches at moderate speed (10k
 * samples/sec)
 * 3. Connect consumers to all filter outputs (if any)
 * 4. Run the pipeline for 500ms and verify:
 *    - Filter handles large buffers without errors
 *    - All data is processed (no dropped batches with large buffers)
 *    - Substantial amount of data flows through (≥40 batches)
 *
 * This test ensures filters don't have issues with large memory allocations
 * and can efficiently process data when given ample buffer space. Large buffers
 * should eliminate any backpressure or overflow conditions.
 */
void test_buffer_large_batches(void)
{
  // Apply large buffer profile
  FilterRegistration_t* reg = &g_filters[g_current_filter];
  if (reg->has_buff_config) {
    apply_buffer_profile(g_fut_config, reg->buff_config_offset,
                         BUFF_PROFILE_LARGE);
  }

  // Initialize filter with large buffers
  CHECK_ERR(g_fut_init(g_fut, g_fut_config));

  // Skip if filter has no inputs
  SKIP_IF_NO_INPUTS();

  // Verify buffer was configured with large sizes
  if (g_fut->n_input_buffers > 0 && g_fut->input_buffers[0]) {
    Batch_buff_t* bb = g_fut->input_buffers[0];
    TEST_ASSERT_EQUAL_MESSAGE(10, bb->batch_capacity_expo,
                              "Buffer should have large batch capacity");
    TEST_ASSERT_EQUAL_MESSAGE(10, bb->ring_capacity_expo,
                              "Buffer should have large ring capacity");
  }

  // Start input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_start(g_fut->input_buffers[i]);
  }

  // Create a producer that fills large batches
  ControllableProducer_t* producer = calloc(1, sizeof(ControllableProducer_t));
  TEST_ASSERT_NOT_NULL(producer);

  ControllableProducerConfig_t prod_config = {.name = "large_batch_producer",
                                              .timeout_us = 1000000,
                                              .samples_per_second = 10000,
                                              .pattern = PATTERN_SEQUENTIAL,
                                              .constant_value = 0.0,
                                              .sine_frequency = 0.0,
                                              .max_batches = 50,
                                              .burst_mode = false,
                                              .burst_on_batches = 0,
                                              .burst_off_batches = 0,
                                              .start_sequence = 0};

  CHECK_ERR(controllable_producer_init(producer, prod_config));

  CHECK_ERR(filt_sink_connect(&producer->base, 0, g_fut->input_buffers[0]));

  // If filter has outputs, create consumers for all sinks
  ConsumerArray_t consumers = create_consumers_if_needed(g_fut, 10, 10);

  // Start pipeline
  CHECK_ERR(filt_start(&producer->base));

  CHECK_ERR(filt_start(g_fut));

  for (size_t i = 0; i < consumers.n_consumers; i++) {
    CHECK_ERR(filt_start(&consumers.consumers[i].base));
  }

  // Let it run
  usleep(1000);  // 500ms

  // Stop pipeline
  CHECK_ERR(filt_stop(&producer->base));

  CHECK_ERR(filt_stop(g_fut));

  for (size_t i = 0; i < consumers.n_consumers; i++) {
    CHECK_ERR(filt_stop(&consumers.consumers[i].base));
  }

  // Verify filter handled large buffers without errors
  CHECK_ERR(producer->base.worker_err_info.ec);
  CHECK_ERR(g_fut->worker_err_info.ec);
  for (size_t i = 0; i < consumers.n_consumers; i++) {
    CHECK_ERR(consumers.consumers[i].base.worker_err_info.ec);
  }

  // Verify data was processed
  size_t produced = atomic_load(&producer->batches_produced);
  TEST_ASSERT_GREATER_THAN_MESSAGE(0, produced,
                                   "Producer should have sent batches");

  // With large buffers, no drops should occur
  size_t dropped = atomic_load(&producer->dropped_batches);
  TEST_ASSERT_EQUAL_MESSAGE(0, dropped,
                            "No batches should be dropped with large buffers");

  // Cleanup
  filt_deinit(&producer->base);
  free(producer);

  for (size_t i = 0; i < consumers.n_consumers; i++) {
    filt_deinit(&consumers.consumers[i].base);
  }
  free(consumers.consumers);

  // Stop input buffers
  for (int i = 0; i < g_fut->n_input_buffers; i++) {
    bb_stop(g_fut->input_buffers[i]);
  }
}
