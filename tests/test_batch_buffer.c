#define _DEFAULT_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include "../bpipe/batch_buffer.h"
#include "unity.h"
#include "unity_internals.h"

Batch_buff_t buff_block;
Batch_buff_t buff_drop;

#define BATCH_CAPACITY_EXPO 4
#define RING_CAPACITY_EXPO 4

char buff[124];

size_t ring_capacity =
    (1 << RING_CAPACITY_EXPO) -
    1;  // One slot always kept empty to distinguish full/empty
size_t batch_capacity = (1 << BATCH_CAPACITY_EXPO);  //

void setUp(void)
{
  BatchBuffer_config config = {.dtype = DTYPE_U32,
                               .overflow_behaviour = OVERFLOW_BLOCK,
                               .ring_capacity_expo = RING_CAPACITY_EXPO,
                               .batch_capacity_expo = BATCH_CAPACITY_EXPO};
  TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK,
                                bb_init(&buff_block, "TEST_BUFF_BLOCK", config),
                                "Failed to init buff_block");
  config.overflow_behaviour = OVERFLOW_DROP_HEAD;
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      Bp_EC_OK, bb_init(&buff_drop, "TEST_BUFF_DROP_HEAD", config),
      "Failed to init buff_drop");
  TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, bb_start(&buff_block),
                                "Failed to start buff_block");
  TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, bb_start(&buff_drop),
                                "Failed to start buff_drop");
}

void tearDown(void)
{
  TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, bb_stop(&buff_block),
                                "Failed to stop buff_block");
  TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, bb_stop(&buff_drop),
                                "Failed to stop buff_drop");
  TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, bb_deinit(&buff_block),
                                "Failed to de-init buff_block");
  TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, bb_deinit(&buff_drop),
                                "Failed to de-init buff_drop");
}

/* Fill the buffer to cappacity with a ramp waveform incrementing by one every
 * sample */
void test_fill_and_empty(void)
{
  TEST_MESSAGE("Filling buff_block");
  TEST_MESSAGE("Before filling");

  bb_print(&buff_block);

  snprintf(buff, sizeof(buff), "Ring Cappacity=%lu, Buff Capacity=%lu",
           ring_capacity, batch_capacity);
  TEST_MESSAGE(buff);
  uint32_t count = 0;
  for (int i = 0; i < ring_capacity; i++) {
    Batch_t* batch = bb_get_head(&buff_block);
    batch->t_ns = i * 1000000;  // Convert to nanoseconds for better display
    batch->period_ns = 2;
    batch->batch_id = i;
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        (char*) buff_block.data_ring + (batch_capacity * sizeof(uint32_t) * i),
        batch->data, "Batch data pointer in unexpected location.");
    for (int ii = 0; ii < batch_capacity; ii++) {
      uint32_t* data = BATCH_GET_SAMPLE_U32(batch, ii);
      *data = count++;
    }
    // Submit the batch to advance the head pointer
    Bp_EC rc = bb_submit(&buff_block, 10000);  // 10ms timeout
    TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, rc, "Failed to submit batch");
  }

  TEST_MESSAGE("After filling");
  bb_print(&buff_block);

  TEST_MESSAGE("Consuming batches");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, bb_get_tail_idx(&buff_block),
                                "Unexpected tail index");

  // Reset count for verification
  count = 0;
  for (int i = 0; i < ring_capacity; i++) {
    Bp_EC err;
    Batch_t* batch = bb_get_tail(&buff_block, 0, &err);
    TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, err, "Error retrieving tail.");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        (char*) buff_block.data_ring + (batch_capacity * sizeof(uint32_t) * i),
        batch->data, "Batch data pointer in unexpected location.");

    TEST_ASSERT_NOT_NULL_MESSAGE(batch, "Failed to get tail batch");
    TEST_ASSERT_EQUAL_INT_MESSAGE(i, batch->batch_id,
                                  "Batch ID not incrementing linearly.");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, batch->period_ns,
                                  "Batch period is unexpected value.");
    TEST_ASSERT_EQUAL_INT_MESSAGE(i * 1000000, batch->t_ns,
                                  "Batch timestamp not incrementing linearly.");

    // Verify data
    for (int ii = 0; ii < batch_capacity; ii++) {
      uint32_t* data = BATCH_GET_SAMPLE_U32(batch, ii);
      TEST_ASSERT_EQUAL_INT_MESSAGE(count, *data,
                                    "Batch Data is not incrementing linearly.");
      count++;
    }
    // Delete the batch to advance tail
    Bp_EC rc = bb_del_tail(&buff_block);
    TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, rc, "Failed to delete batch");
  }

  TEST_MESSAGE("After consuming all batches");
  bb_print(&buff_block);
}

void* submitter(void* arg)
{
  Bp_EC* ec = (Bp_EC*) arg;
  *ec = bb_submit(&buff_block, 20000);
  return NULL;
}

void* consumer(void* arg)
{
  Bp_EC* ec = (Bp_EC*) arg;
  /* we just care if we timed out */
  (void) bb_get_tail(&buff_block, 20000, ec);
  return NULL;
}

/* */
void test_overflow_block(void)
{
  Batch_t* batch;
  size_t count = 0;
  for (int i = 0; i < ring_capacity; i++) {
    batch = bb_get_head(&buff_block);
    batch->t_ns = i * 1000000;  // Convert to nanoseconds for better display
    batch->period_ns = 2;
    batch->batch_id = i;
    for (int ii = 0; ii < batch_capacity; ii++) {
      uint32_t* data = BATCH_GET_SAMPLE_U32(batch, ii);
      *data = count++;
    }
    // Submit the batch to advance the head pointer
    Bp_EC rc = bb_submit(&buff_block, 10000);  // 10ms timeout
    TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, rc, "Failed to submit batch");
  }

  /* This slot should be accessible but not possible to submit */
  batch = bb_get_head(&buff_block);

  /* Try a 5ms timeout */
  Bp_EC ec;
  long long ts_before = now_ns(CLOCK_MONOTONIC);
  ec = bb_submit(&buff_block, 5000);
  long long ts_after = now_ns(CLOCK_MONOTONIC);
  long long elapsed_ns = ts_after - ts_before;

  TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_TIMEOUT, ec, "Expected timeout fail.");
  /* Timeout should be at least 4ms (allowing 1ms tolerance for system
   * scheduling) */
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(4000000, elapsed_ns,
                                       "Timeout was shorter than 4ms");
  /* But not longer than 10ms */
  TEST_ASSERT_LESS_THAN_INT_MESSAGE(10000000, elapsed_ns,
                                    "Timeout took longer than 10ms");

  /* test that stopping the batch buffer imediately unblocks */

  /* Bock for 1s */
  pthread_t test_blocked_submitter;

  Bp_EC submitter_ec;
  ts_before = now_ns(CLOCK_MONOTONIC);
  pthread_create(&test_blocked_submitter, NULL, submitter,
                 (void*) &submitter_ec);

  struct timespec sleeptime = {.tv_nsec = 10000000};  // 10ms
  nanosleep(&sleeptime, NULL);

  ec = bb_stop(&buff_block);
  TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, ec, "Failed to stop.");

  TEST_ASSERT_EQUAL_INT_MESSAGE(0, pthread_join(test_blocked_submitter, NULL),
                                "Failed to join");
  ts_after = now_ns(CLOCK_MONOTONIC);
  elapsed_ns = ts_after - ts_before;

  TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_STOPPED, submitter_ec,
                                "Expected stopped fail.");

  /* Timeout should be at least 10ms */
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(10000000, elapsed_ns,
                                       "Join quicker than expected.");
  /* But not longer than 20ms */
  TEST_ASSERT_LESS_THAN_INT_MESSAGE(12000000, elapsed_ns,
                                    "Join slower than expected. ");

  TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, ec, "Shoudn't have timed out");
}

/* Demonstrate blocked consumer threads return after timout. */
void test_empty_blocking_consume_timeout()
{
  // Submit the batch to advance the head pointer
  pthread_t submitter_thread;
  Bp_EC consumer_ec;

  /* Create a thread that will be blocked by the buffer being empty */
  long long ts_before = now_ns(CLOCK_MONOTONIC);
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      0,
      pthread_create(&submitter_thread, NULL, consumer, (void*) &consumer_ec),
      "Failed to create consumer thread.");

  /* Join the cusumer thread, this will only be possible if the thread is
   * un-blocked.*/
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, pthread_join(submitter_thread, NULL),
                                "Failed to join");
  long long ts_after = now_ns(CLOCK_MONOTONIC);
  long long elapse_ns = ts_after - ts_before;

  /* Error code should be ok since we provided data before the timeout */
  TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_TIMEOUT, consumer_ec,
                                "Expected timeout.");
  /* Timeout is 20ms so join time should be symilar.*/
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(20000000, elapse_ns,
                                       "Join quicker than expected.");
  /* But not longer than 20ms */
  TEST_ASSERT_LESS_THAN_INT_MESSAGE(22000000, elapse_ns,
                                    "Join slower than expected. ");
}

/* Demonstrate blocked threads will return if the bb_stop() is called */
void test_empty_stop_unblock()
{
  // Submit the batch to advance the head pointer
  pthread_t submitter_thread;
  Bp_EC consumer_ec;

  /* Create a thread that will be blocked by the buffer being empty */
  long long ts_before = now_ns(CLOCK_MONOTONIC);
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      0,
      pthread_create(&submitter_thread, NULL, consumer, (void*) &consumer_ec),
      "Failed to create consumer thread.");

  /* Wait for 10ms before submitting a batch which should ublock the consumer */
  struct timespec sleeptime = {.tv_nsec = 10000000};  // 10ms
  nanosleep(&sleeptime, NULL);
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      Bp_EC_OK, bb_stop(&buff_block),
      "Failed to stop.");  // should be empty so no timeout needed

  /* Join the cusumer thread, this will only be possible if the thread is
   * un-blocked.*/
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, pthread_join(submitter_thread, NULL),
                                "Failed to join.");
  long long ts_after = now_ns(CLOCK_MONOTONIC);
  long long elapse_ns = ts_after - ts_before;

  /* Error code should be ok since we provided data before the timeout */
  TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_STOPPED, consumer_ec,
                                "Expected stopped fail.");
  /* Join time should be nearly imediate so ~10 seconds accounting for the wait.
   */
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(10000000, elapse_ns,
                                       "Join quicker than expected.");
  /* But not longer than 20ms */
  TEST_ASSERT_LESS_THAN_INT_MESSAGE(12000000, elapse_ns,
                                    "Join slower than expected. ");
}

/* Demonstrate ability to un-block consumer thread when new data is available */
void test_empty_blocking_consume()
{
  // Submit the batch to advance the head pointer
  pthread_t submitter_thread;
  Bp_EC consumer_ec;

  /* Create a thread that will be blocked by the buffer being empty */
  long long ts_before = now_ns(CLOCK_MONOTONIC);
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      0,
      pthread_create(&submitter_thread, NULL, consumer, (void*) &consumer_ec),
      "Failed to create consumer thread.");

  /* Wait for 10ms before submitting a batch which should ublock the consumer */
  struct timespec sleeptime = {.tv_nsec = 10000000};  // 10ms
  nanosleep(&sleeptime, NULL);
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      Bp_EC_OK, bb_submit(&buff_block, 0),
      "Failed to sumbmit");  // should be empty so no timeout needed

  /* Join the cusumer thread, this will only be possible if the thread is
   * un-blocked.*/
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, pthread_join(submitter_thread, NULL),
                                "Failed to join");
  long long ts_after = now_ns(CLOCK_MONOTONIC);
  long long elapse_ns = ts_after - ts_before;

  /* Error code should be ok since we provided data before the timeout */
  TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, consumer_ec,
                                "Expected stopped fail.");
  /* Join time should be nearly imediate so ~10 seconds accounting for the wait.
   */
  TEST_ASSERT_GREATER_THAN_INT_MESSAGE(10000000, elapse_ns,
                                       "Join quicker than expected.");
  /* But not longer than 20ms */
  TEST_ASSERT_LESS_THAN_INT_MESSAGE(12000000, elapse_ns,
                                    "Join slower than expected. ");
}

/* Test OVERFLOW_DROP_TAIL behavior */
void test_overflow_drop_tail(void)
{
  TEST_MESSAGE("Testing OVERFLOW_DROP_TAIL behavior");

  // Setup buffer with DROP_TAIL mode
  Batch_buff_t buff_drop_tail;
  BatchBuffer_config config = {
      .dtype = DTYPE_U32,
      .overflow_behaviour = OVERFLOW_DROP_TAIL,
      .ring_capacity_expo = 3,  // 8 slots, 7 usable
      .batch_capacity_expo = 2  // 4 samples per batch
  };
  TEST_ASSERT_EQUAL_INT(Bp_EC_OK,
                        bb_init(&buff_drop_tail, "DROP_TAIL", config));
  TEST_ASSERT_EQUAL_INT(Bp_EC_OK, bb_start(&buff_drop_tail));

  // Phase 1: Fill buffer completely (7 batches)
  TEST_MESSAGE("Phase 1: Filling buffer");
  for (int i = 0; i < 7; i++) {
    Batch_t* batch = bb_get_head(&buff_drop_tail);
    batch->batch_id = i;
    batch->t_ns = i * 1000;
    // Fill with recognizable pattern
    for (int j = 0; j < 4; j++) {
      uint32_t* data = BATCH_GET_SAMPLE_U32(batch, j);
      *data = i * 100 + j;  // e.g., batch 0: 0,1,2,3; batch 1: 100,101,102,103
    }
    TEST_ASSERT_EQUAL_INT(Bp_EC_OK, bb_submit(&buff_drop_tail, 1000));
  }

  // Verify buffer is full
  TEST_ASSERT_TRUE_MESSAGE(bb_isfull_lockfree(&buff_drop_tail),
                           "Buffer should be full");

  // Phase 2: Submit 8th batch - should succeed by dropping oldest
  TEST_MESSAGE("Phase 2: Testing DROP_TAIL - submitting when full");
  Batch_t* batch = bb_get_head(&buff_drop_tail);
  batch->batch_id = 7;
  batch->t_ns = 7000;
  for (int j = 0; j < 4; j++) {
    uint32_t* data = BATCH_GET_SAMPLE_U32(batch, j);
    *data = 700 + j;
  }
  TEST_ASSERT_EQUAL_INT(Bp_EC_OK, bb_submit(&buff_drop_tail, 1000));

  // Phase 3: Verify oldest (batch 0) was dropped
  TEST_MESSAGE("Phase 3: Verifying oldest batch was dropped");
  Bp_EC err;
  batch = bb_get_tail(&buff_drop_tail, 1000, &err);
  TEST_ASSERT_EQUAL_INT(Bp_EC_OK, err);
  TEST_ASSERT_EQUAL_INT(1, batch->batch_id);  // Batch 0 was dropped!
  TEST_ASSERT_EQUAL_INT(1000, batch->t_ns);

  // Verify data integrity
  uint32_t* data = BATCH_GET_SAMPLE_U32(batch, 0);
  TEST_ASSERT_EQUAL_INT(100, *data);  // First sample of batch 1

  // Check dropped counter
  uint64_t dropped = atomic_load(&buff_drop_tail.consumer.dropped_by_producer);
  TEST_ASSERT_EQUAL_INT(1, dropped);

  // Cleanup
  bb_stop(&buff_drop_tail);
  bb_deinit(&buff_drop_tail);
}

/* Test concurrent producer/consumer with DROP_TAIL */
typedef struct {
  Batch_buff_t* buff;
  int start_id;
  int count;
  Bp_EC result;
} producer_args_t;

void* drop_tail_producer(void* arg)
{
  producer_args_t* args = (producer_args_t*) arg;
  for (int i = 0; i < args->count; i++) {
    Batch_t* batch = bb_get_head(args->buff);
    batch->batch_id = args->start_id + i;
    batch->t_ns = (args->start_id + i) * 1000;
    // Fill with test data
    for (int j = 0; j < (1 << args->buff->batch_capacity_expo); j++) {
      uint32_t* data = BATCH_GET_SAMPLE_U32(batch, j);
      *data = batch->batch_id * 100 + j;
    }
    args->result = bb_submit(args->buff, 1000);
    if (args->result != Bp_EC_OK) break;
    usleep(100);  // Small delay to simulate real producer
  }
  return NULL;
}

void test_drop_tail_concurrent(void)
{
  TEST_MESSAGE("Testing concurrent DROP_TAIL behavior");

  // Create small buffer to force drops
  Batch_buff_t buff;
  BatchBuffer_config config = {
      .dtype = DTYPE_U32,
      .overflow_behaviour = OVERFLOW_DROP_TAIL,
      .ring_capacity_expo = 2,  // 4 slots, 3 usable
      .batch_capacity_expo = 2  // 4 samples per batch
  };
  TEST_ASSERT_EQUAL_INT(Bp_EC_OK, bb_init(&buff, "CONCURRENT", config));
  TEST_ASSERT_EQUAL_INT(Bp_EC_OK, bb_start(&buff));

  // Start fast producer
  pthread_t producer;
  producer_args_t args = {&buff, 0, 20, Bp_EC_OK};
  TEST_ASSERT_EQUAL_INT(
      0, pthread_create(&producer, NULL, drop_tail_producer, &args));

  // Slow consumer
  int last_seen_id = -1;
  int gaps_detected = 0;
  Bp_EC err;

  for (int i = 0; i < 10; i++) {
    Batch_t* batch = bb_get_tail(&buff, 5000, &err);
    if (err == Bp_EC_OK && batch) {
      int id = batch->batch_id;
      // Check for gaps
      if (last_seen_id >= 0 && id > last_seen_id + 1) {
        gaps_detected += (id - last_seen_id - 1);
        TEST_MESSAGE("Gap detected in sequence");
      }
      last_seen_id = id;

      // Verify data integrity
      uint32_t* data = BATCH_GET_SAMPLE_U32(batch, 0);
      TEST_ASSERT_EQUAL_INT(id * 100, *data);

      bb_del_tail(&buff);
    }
    usleep(10000);  // Slow consumer (10ms)
  }

  pthread_join(producer, NULL);

  // Verify some batches were dropped
  uint64_t dropped = atomic_load(&buff.consumer.dropped_by_producer);
  TEST_ASSERT_GREATER_THAN(0, dropped);
  TEST_ASSERT_GREATER_THAN(0, gaps_detected);
  TEST_ASSERT_EQUAL_INT(Bp_EC_OK, args.result);

  // Cleanup remaining batches
  while (!bb_isempy_lockfree(&buff)) {
    bb_del_tail(&buff);
  }

  bb_stop(&buff);
  bb_deinit(&buff);
}

int main(int argc, char* argv[])
{
  UNITY_BEGIN();
  RUN_TEST(test_fill_and_empty);
  RUN_TEST(test_overflow_block);
  RUN_TEST(test_empty_stop_unblock);
  RUN_TEST(test_empty_blocking_consume_timeout);
  RUN_TEST(test_empty_blocking_consume);
  RUN_TEST(test_overflow_drop_tail);
  RUN_TEST(test_drop_tail_concurrent);
  return UNITY_END();
}
