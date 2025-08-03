/**
 * @file test_buffer_edge_cases.c
 * @brief Test filters with edge case buffer configurations
 */

#include "common.h"
#include "test_utils.h"

// Helper structure to manage multiple consumers
typedef struct {
  ControllableConsumer_t* consumers;  // Array of consumers (not array of pointers)
  size_t n_consumers;
} ConsumerArray_t;

// Helper function to create consumers for all filter outputs
static ConsumerArray_t create_consumers_if_needed(Filter_t* filter, 
                                                  uint8_t batch_expo,
                                                  uint8_t ring_expo) {
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
      .slow_start_batches = 0
    };
    
    CHECK_ERR(controllable_consumer_init(&result.consumers[i], cons_config));
    CHECK_ERR(filt_sink_connect(filter, i, result.consumers[i].base.input_buffers[0]));
  }
  
  return result;
}

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
    .start_sequence = 0
  };
  
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
  usleep(100000);  // 100ms
  
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

void test_buffer_overflow_drop_head(void)
{
  // Apply small buffer profile
  FilterRegistration_t* reg = &g_filters[g_current_filter];
  if (reg->has_buff_config) {
    apply_buffer_profile(g_fut_config, reg->buff_config_offset, 
                        BUFF_PROFILE_SMALL);
    
    // Override overflow behavior to DROP_HEAD
    BatchBuffer_config* buff_config = 
      (BatchBuffer_config*)((char*)g_fut_config + reg->buff_config_offset);
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
    .timeout_us = 10000,  // Short timeout to trigger overflow
    .samples_per_second = 100000,  // Fast production
    .pattern = PATTERN_SEQUENTIAL,
    .constant_value = 0.0,
    .sine_frequency = 0.0,
    .max_batches = 100,
    .burst_mode = true,
    .burst_on_batches = 20,  // Burst to trigger overflow
    .burst_off_batches = 5,
    .start_sequence = 0
  };
  
  CHECK_ERR(controllable_producer_init(producer, prod_config));
  
  CHECK_ERR(filt_sink_connect(&producer->base, 0, g_fut->input_buffers[0]));
  
  // If filter has outputs, create consumers for all sinks
  ConsumerArray_t consumers = create_consumers_if_needed(g_fut, 4, 3);
  
  // Start producer but not filter (to cause overflow)
  CHECK_ERR(filt_start(&producer->base));
  
  // Let producer fill buffer
  usleep(50000);  // 50ms
  
  // Now start filter to process
  CHECK_ERR(filt_start(g_fut));
  
  for (size_t i = 0; i < consumers.n_consumers; i++) {
    CHECK_ERR(filt_start(&consumers.consumers[i].base));
  }
  
  // Let it run
  usleep(200000);  // 200ms
  
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
  size_t dropped = atomic_load(&producer->dropped_batches);
  
  TEST_ASSERT_GREATER_THAN_MESSAGE(0, produced,
                                  "Producer should have sent batches");
  TEST_ASSERT_GREATER_THAN_MESSAGE(0, dropped,
                                  "Some batches should have been dropped");
  
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

void test_buffer_overflow_drop_tail(void)
{
  // Apply small buffer profile
  FilterRegistration_t* reg = &g_filters[g_current_filter];
  if (reg->has_buff_config) {
    apply_buffer_profile(g_fut_config, reg->buff_config_offset, 
                        BUFF_PROFILE_SMALL);
    
    // Override overflow behavior to DROP_TAIL
    BatchBuffer_config* buff_config = 
      (BatchBuffer_config*)((char*)g_fut_config + reg->buff_config_offset);
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
    .timeout_us = 10000,  // Short timeout to trigger overflow
    .samples_per_second = 100000,  // Fast production
    .pattern = PATTERN_SEQUENTIAL,
    .constant_value = 0.0,
    .sine_frequency = 0.0,
    .max_batches = 100,
    .burst_mode = true,
    .burst_on_batches = 20,  // Burst to trigger overflow
    .burst_off_batches = 5,
    .start_sequence = 0
  };
  
  CHECK_ERR(controllable_producer_init(producer, prod_config));
  
  CHECK_ERR(filt_sink_connect(&producer->base, 0, g_fut->input_buffers[0]));
  
  // If filter has outputs, create consumers for all sinks
  ConsumerArray_t consumers = create_consumers_if_needed(g_fut, 4, 3);
  
  // Start producer but not filter (to cause overflow)
  CHECK_ERR(filt_start(&producer->base));
  
  // Let producer fill buffer
  usleep(50000);  // 50ms
  
  // Now start filter to process
  CHECK_ERR(filt_start(g_fut));
  
  for (size_t i = 0; i < consumers.n_consumers; i++) {
    CHECK_ERR(filt_start(&consumers.consumers[i].base));
  }
  
  // Let it run
  usleep(200000);  // 200ms
  
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
  size_t dropped = atomic_load(&producer->dropped_batches);
  
  TEST_ASSERT_GREATER_THAN_MESSAGE(0, produced,
                                  "Producer should have sent batches");
  TEST_ASSERT_GREATER_THAN_MESSAGE(0, dropped,
                                  "Some batches should have been dropped");
  
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
  
  ControllableProducerConfig_t prod_config = {
    .name = "large_batch_producer",
    .timeout_us = 1000000,
    .samples_per_second = 10000,
    .pattern = PATTERN_SEQUENTIAL,
    .constant_value = 0.0,
    .sine_frequency = 0.0,
    .max_batches = 50,
    .burst_mode = false,
    .burst_on_batches = 0,
    .burst_off_batches = 0,
    .start_sequence = 0
  };
  
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
  usleep(500000);  // 500ms
  
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