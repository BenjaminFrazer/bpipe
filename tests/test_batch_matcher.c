#include <pthread.h>
#include <string.h>
#include <time.h>
#include "batch_matcher.h"
#include "core.h"
#include "unity.h"

// Test fixture
typedef struct {
  BatchMatcher_t matcher;
  Filter_t source;
  Filter_t sink;
  pthread_t source_thread;
  bool source_running;
} TestFixture;

static TestFixture fixture;

// Dummy worker that does nothing (for manual data pushing)
void* dummy_worker(void* arg)
{
  Filter_t* f = (Filter_t*) arg;
  // Just wait to be stopped
  while (f->running) {
    struct timespec sleep_time = {0, 100000000};  // 100ms
    nanosleep(&sleep_time, NULL);
  }
  return NULL;
}

// Simple source worker that generates regular data
void* test_source_worker(void* arg)
{
  Filter_t* f = (Filter_t*) arg;
  TestFixture* fix = (TestFixture*) ((char*) f - offsetof(TestFixture, source));

  uint64_t t_ns = 0;             // Start at t=0 for easy alignment
  uint64_t period_ns = 1000000;  // 1ms period (1kHz)
  uint32_t batch_id = 0;

  while (fix->source_running) {
    Batch_t* batch = bb_get_head(f->sinks[0]);
    if (batch == NULL) {
      break;
    }

    // Fill batch with test data
    batch->t_ns = t_ns;
    batch->period_ns = period_ns;
    batch->head = 0;
    batch->tail = 64;  // 64 samples per batch
    batch->batch_id = batch_id++;
    batch->ec = Bp_EC_OK;

    // Fill with sequential values
    float* data = (float*) batch->data;
    for (int i = 0; i < 64; i++) {
      data[i] = (float) (t_ns / period_ns + i);
    }

    t_ns += 64 * period_ns;

    bb_submit(f->sinks[0], 1000000);
  }

  // Send completion
  Batch_t* batch = bb_get_head(f->sinks[0]);
  if (batch != NULL) {
    batch->ec = Bp_EC_COMPLETE;
    batch->head = 0;
    batch->tail = 0;
    bb_submit(f->sinks[0], 1000000);
  }

  return NULL;
}

void setUp(void) { memset(&fixture, 0, sizeof(fixture)); }

void tearDown(void)
{
  // Ensure threads are stopped
  if (fixture.source_running) {
    fixture.source_running = false;
    pthread_join(fixture.source_thread, NULL);
  }

  // Deinit filters if initialized
  if (fixture.matcher.base.worker != NULL) {
    filt_stop(&fixture.matcher.base);
    filt_deinit(&fixture.matcher.base);
  }
  if (fixture.source.worker != NULL) {
    filt_deinit(&fixture.source);
  }
  if (fixture.sink.worker != NULL) {
    filt_deinit(&fixture.sink);
  }
}

void test_basic_batch_matching(void)
{
  // Setup source with 64-sample batches
  Core_filt_config_t source_config = {
      .name = "test_source",
      .filt_type = FILT_T_MAP,
      .size = sizeof(Filter_t),
      .n_inputs = 0,
      .max_supported_sinks = 1,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,  // 64 samples
                      .ring_capacity_expo = 4,   // 16 batches
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000000,
      .worker = test_source_worker};
  TEST_ASSERT_EQUAL(Bp_EC_OK, filt_init(&fixture.source, source_config));

  // Setup BatchMatcher
  BatchMatcher_config_t matcher_config = {
      .name = "test_matcher",
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,  // 64 samples input
                      .ring_capacity_expo = 4,   // 16 batches
                      .overflow_behaviour = OVERFLOW_BLOCK}};
  TEST_ASSERT_EQUAL(Bp_EC_OK,
                    batch_matcher_init(&fixture.matcher, matcher_config));

  // Setup sink with 128-sample batches
  Core_filt_config_t sink_config = {
      .name = "test_sink",
      .filt_type = FILT_T_MATCHED_PASSTHROUGH,
      .size = sizeof(Filter_t),
      .n_inputs = 1,
      .max_supported_sinks = 0,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 7,  // 128 samples
                      .ring_capacity_expo = 4,   // 16 batches
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000000,
      .worker = matched_passthroug};
  TEST_ASSERT_EQUAL(Bp_EC_OK, filt_init(&fixture.sink, sink_config));

  // Connect pipeline: source -> matcher -> sink
  TEST_ASSERT_EQUAL(Bp_EC_OK,
                    filt_sink_connect(&fixture.source, 0,
                                      &fixture.matcher.base.input_buffers[0]));
  TEST_ASSERT_EQUAL(Bp_EC_OK,
                    filt_sink_connect(&fixture.matcher.base, 0,
                                      &fixture.sink.input_buffers[0]));

  // Verify auto-detection worked
  TEST_ASSERT_TRUE(fixture.matcher.size_detected);
  TEST_ASSERT_EQUAL(128, fixture.matcher.output_batch_samples);

  // Start filters
  fixture.source_running = true;
  TEST_ASSERT_EQUAL(
      Bp_EC_OK, pthread_create(&fixture.source_thread, NULL, test_source_worker,
                               &fixture.source));
  TEST_ASSERT_EQUAL(Bp_EC_OK, filt_start(&fixture.matcher.base));
  TEST_ASSERT_EQUAL(Bp_EC_OK, filt_start(&fixture.sink));

  // Let it run briefly
  struct timespec sleep_time = {0, 100000000};  // 100ms
  nanosleep(&sleep_time, NULL);

  // Check output has 128-sample batches aligned to t=0
  Bp_EC err;
  Batch_t* output = bb_get_tail(&fixture.sink.input_buffers[0], 1000000, &err);
  if (err == Bp_EC_OK) {
    TEST_ASSERT_EQUAL(128, output->tail - output->head);
    TEST_ASSERT_EQUAL(
        0, output->t_ns % (128 * 1000000));  // Aligned to batch period
    bb_del_tail(&fixture.sink.input_buffers[0]);
  }
}

void test_auto_detection(void)
{
  // Create matcher without specifying size
  BatchMatcher_config_t config = {
      .name = "auto_matcher",
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 4,
                      .overflow_behaviour = OVERFLOW_BLOCK}};
  TEST_ASSERT_EQUAL(Bp_EC_OK, batch_matcher_init(&fixture.matcher, config));

  // Initially, size should not be detected
  TEST_ASSERT_FALSE(fixture.matcher.size_detected);
  TEST_ASSERT_EQUAL(0, fixture.matcher.output_batch_samples);

  // Create sink with batch_capacity_expo = 8 (256 samples)
  Core_filt_config_t sink_config = {
      .name = "test_sink",
      .filt_type = FILT_T_MATCHED_PASSTHROUGH,
      .size = sizeof(Filter_t),
      .n_inputs = 1,
      .max_supported_sinks = 0,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 8,  // 256 samples
                      .ring_capacity_expo = 4,
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000000,
      .worker = matched_passthroug};
  TEST_ASSERT_EQUAL(Bp_EC_OK, filt_init(&fixture.sink, sink_config));

  // Connect sink
  TEST_ASSERT_EQUAL(Bp_EC_OK,
                    filt_sink_connect(&fixture.matcher.base, 0,
                                      &fixture.sink.input_buffers[0]));

  // Now size should be detected
  TEST_ASSERT_TRUE(fixture.matcher.size_detected);
  TEST_ASSERT_EQUAL(256, fixture.matcher.output_batch_samples);
}

void test_no_sink_error(void)
{
  // Create matcher
  BatchMatcher_config_t config = {
      .name = "no_sink_matcher",
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 4,
                      .overflow_behaviour = OVERFLOW_BLOCK}};
  TEST_ASSERT_EQUAL(Bp_EC_OK, batch_matcher_init(&fixture.matcher, config));

  // Try to start without connecting sink
  TEST_ASSERT_EQUAL(Bp_EC_NO_SINK, filt_start(&fixture.matcher.base));
}

void test_phase_validation(void)
{
  // Setup source that starts at t=12345ns (non-zero phase)
  Core_filt_config_t source_config = {
      .name = "phase_source",
      .filt_type = FILT_T_MAP,
      .size = sizeof(Filter_t),
      .n_inputs = 0,
      .max_supported_sinks = 1,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 4,
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000000,
      .worker = dummy_worker  // Use dummy worker for manual data pushing
  };
  TEST_ASSERT_EQUAL(Bp_EC_OK, filt_init(&fixture.source, source_config));

  // Setup BatchMatcher and sink
  BatchMatcher_config_t matcher_config = {
      .name = "phase_matcher",
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 4,
                      .overflow_behaviour = OVERFLOW_BLOCK}};
  TEST_ASSERT_EQUAL(Bp_EC_OK,
                    batch_matcher_init(&fixture.matcher, matcher_config));

  Core_filt_config_t sink_config = {
      .name = "phase_sink",
      .filt_type = FILT_T_MATCHED_PASSTHROUGH,
      .size = sizeof(Filter_t),
      .n_inputs = 1,
      .max_supported_sinks = 0,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 7,
                      .ring_capacity_expo = 4,
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000000,
      .worker = matched_passthroug};
  TEST_ASSERT_EQUAL(Bp_EC_OK, filt_init(&fixture.sink, sink_config));

  // Connect pipeline
  TEST_ASSERT_EQUAL(Bp_EC_OK,
                    filt_sink_connect(&fixture.source, 0,
                                      &fixture.matcher.base.input_buffers[0]));
  TEST_ASSERT_EQUAL(Bp_EC_OK,
                    filt_sink_connect(&fixture.matcher.base, 0,
                                      &fixture.sink.input_buffers[0]));

  // Start source and matcher
  TEST_ASSERT_EQUAL(Bp_EC_OK, filt_start(&fixture.source));
  TEST_ASSERT_EQUAL(Bp_EC_OK, filt_start(&fixture.matcher.base));

  // Push a batch with non-aligned timestamp
  Batch_t* batch = bb_get_head(&fixture.matcher.base.input_buffers[0]);
  TEST_ASSERT_NOT_NULL(batch);

  batch->t_ns = 12345000;      // 12.345ms - phase offset of 345us
  batch->period_ns = 1000000;  // 1ms period
  batch->head = 0;
  batch->tail = 64;
  batch->ec = Bp_EC_OK;

  bb_submit(&fixture.matcher.base.input_buffers[0], 1000000);

  // Wait for worker to process
  struct timespec sleep_time = {0, 100000000};  // 100ms
  nanosleep(&sleep_time, NULL);

  // Check that worker detected phase error
  TEST_ASSERT_EQUAL(Bp_EC_PHASE_ERROR, fixture.matcher.base.worker_err_info.ec);
  TEST_ASSERT_FALSE(fixture.matcher.base.running);
}

void test_input_already_matched(void)
{
  // When input batch size equals sink batch size, should be efficient
  // passthrough

  // Setup with matching sizes (64 samples everywhere)
  BatchMatcher_config_t matcher_config = {
      .name = "passthrough_matcher",
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,  // 64 samples
                      .ring_capacity_expo = 4,
                      .overflow_behaviour = OVERFLOW_BLOCK}};
  TEST_ASSERT_EQUAL(Bp_EC_OK,
                    batch_matcher_init(&fixture.matcher, matcher_config));

  Core_filt_config_t sink_config = {
      .name = "matched_sink",
      .filt_type = FILT_T_MATCHED_PASSTHROUGH,
      .size = sizeof(Filter_t),
      .n_inputs = 1,
      .max_supported_sinks = 0,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,  // Also 64 samples
                      .ring_capacity_expo = 4,
                      .overflow_behaviour = OVERFLOW_BLOCK},
      .timeout_us = 1000000,
      .worker = matched_passthroug};
  TEST_ASSERT_EQUAL(Bp_EC_OK, filt_init(&fixture.sink, sink_config));

  // Connect
  TEST_ASSERT_EQUAL(Bp_EC_OK,
                    filt_sink_connect(&fixture.matcher.base, 0,
                                      &fixture.sink.input_buffers[0]));

  // Both should have same size
  TEST_ASSERT_EQUAL(64, fixture.matcher.output_batch_samples);
}

int main(void)
{
  UNITY_BEGIN();

  RUN_TEST(test_basic_batch_matching);
  RUN_TEST(test_auto_detection);
  RUN_TEST(test_no_sink_error);
  RUN_TEST(test_phase_validation);
  RUN_TEST(test_input_already_matched);

  return UNITY_END();
}
