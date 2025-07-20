#include <bits/pthreadtypes.h>
#include <bits/time.h>
#include <bits/types/struct_iovec.h>
#include <bits/types/timer_t.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "unity_internals.h"
#define _DEFAULT_SOURCE
#include <unistd.h>
#include "../bpipe/batch_buffer.h"
#include "../bpipe/core.h"
#include "time.h"
#include "unity.h"
#include "test_utils.h"

#define BATCH_CAPACITY_EXPO 4
#define RING_CAPACITY_EXPO 4

char buff[124];

size_t ring_capacity = (1 << RING_CAPACITY_EXPO) - 1;  // One slot always kept empty to distinguish full/empty
size_t batch_capacity = (1 << BATCH_CAPACITY_EXPO);  //
                                                     //
                                                     //
const BatchBuffer_config config = {.dtype = DTYPE_U32,
                                   .overflow_behaviour = OVERFLOW_BLOCK,
                                   .ring_capacity_expo = RING_CAPACITY_EXPO,
                                   .batch_capacity_expo = BATCH_CAPACITY_EXPO};

Core_filt_config_t filter_config = {
    .name = "TEST_FILTER",
    .size = sizeof(Filter_t),
    .worker = &matched_passthroug,
    .timeout_us = 10000,  // infinite timeout
    .max_supported_sinks = 1,
    .n_inputs = 1,
    .filt_type = FILT_T_MATCHED_PASSTHROUGH,
    .buff_config = config,
};

Filter_t filt1;
Filter_t filt2;
Filter_t filt3;
Batch_buff_t output;

struct timespec ts_1ms = {.tv_nsec = 1000000};  // 10ms

Bp_EC _ec;


void setUp(void)
{
  TEST_MESSAGE("setUp entry");
  CHECK_ERR(bb_init(&output, "OUTPUT", config));
  CHECK_ERR(filt_init(&filt1, filter_config));
  CHECK_ERR(filt_init(&filt2, filter_config));
  CHECK_ERR(filt_init(&filt3, filter_config));
  TEST_MESSAGE("setUp exit");
}

void tearDown(void)
{
  TEST_MESSAGE("tearDown entry");
  CHECK_ERR(filt_stop(&filt1));
  CHECK_ERR(filt_stop(&filt2));
  CHECK_ERR(filt_stop(&filt3));
  CHECK_ERR(bb_stop(&output));
  CHECK_ERR(bb_deinit(&output));
  CHECK_ERR(filt_sink_disconnect(&filt1, 0));
  CHECK_ERR(filt_deinit(&filt1));
  TEST_MESSAGE("Teardown exit");
}

void test_init_and_teardown() { TEST_MESSAGE("test_init_and_teardown"); }

Batch_t* batch_in;
Batch_t* batch_out;
Bp_EC err;
uint32_t count_in = 0;
uint32_t count_out = 0;

void test_data_passthrough_single_thread(void)
{
  TEST_MESSAGE("Data passthrough entry");

  /* Setup */
  CHECK_ERR(filt_sink_connect(&filt1, 0, &output));
  CHECK_ERR(bb_start(&output));
  CHECK_ERR(filt_start(&filt1));
  CHECK_ERR(filt1.worker_err_info.ec);

  /* Main */
  for (int i = 0; i < (ring_capacity * 2); i++) {
    batch_in = bb_get_head(&filt1.input_buffers[0]);
    for (int ii = 0; ii < batch_capacity; ii++) {
      *((uint32_t*) batch_in->data + ii) = count_in;
      count_in++;
    }
    // TEST_MESSAGE("Submitting batch to input");
    CHECK_ERR(
        bb_submit(&filt1.input_buffers[0], 10000));  // should always be space
    // wait a bit and check if there has been a worker error
    nanosleep(&ts_1ms, NULL);
    CHECK_ERR(filt1.worker_err_info.ec);  //
    // TEST_MESSAGE("Getting output tail");
    //  TEST_MESSAGE("Input");
    //  bb_print(&filt.input_buffers[0]);
    //  TEST_MESSAGE("Output");
    //  bb_print(&output);
    batch_out = bb_get_tail(&output, 10000, &err);
    CHECK_ERR(err);  // should always be space
    for (int ii = 0; ii < batch_capacity; ii++) {
      uint32_t out = *((uint32_t*) batch_out->data + ii);
      TEST_ASSERT_EQUAL_INT_MESSAGE(
          count_out, out, "Output data is not incrementing linearly.");
      count_out++;
    }
    CHECK_ERR(bb_del_tail(&output));
  }
  TEST_MESSAGE("Data passthrough exit");
}

void test_filter_cascade(void)
{
  Bp_EC err;
  TEST_MESSAGE("Filter cascade entry");

  /* Setup */
  count_out = 0;
  count_in = 0;
  CHECK_ERR(filt_sink_connect(&filt1, 0, &filt2.input_buffers[0]));
  CHECK_ERR(filt_sink_connect(&filt2, 0, &filt3.input_buffers[0]));
  CHECK_ERR(filt_sink_connect(&filt3, 0, &output));
  CHECK_ERR(filt_start(&filt1));
  CHECK_ERR(filt_start(&filt2));
  CHECK_ERR(filt_start(&filt3));
  CHECK_ERR(bb_start(&output));
  CHECK_ERR(filt1.worker_err_info.ec);
  CHECK_ERR(filt2.worker_err_info.ec);
  CHECK_ERR(filt3.worker_err_info.ec);

  /* Main */
  for (int i = 0; i < (ring_capacity * 4); i++) {
    batch_in = bb_get_head(&filt1.input_buffers[0]);
    for (int ii = 0; ii < batch_capacity; ii++) {
      *((uint32_t*) batch_in->data + ii) = count_in;
      count_in++;
    }
    // TEST_MESSAGE("Submitting batch to input");
    CHECK_ERR(bb_submit(&filt1.input_buffers[0], 1000));  //
  }

  TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_TIMEOUT,
                                bb_submit(&filt1.input_buffers[0], 1000),
                                "Expected timeout");

  for (int i = 0; i < (ring_capacity * 4); i++) {
    batch_out = bb_get_tail(&output, 1000, &err);
    CHECK_ERR(err);  //
    for (int ii = 0; ii < batch_capacity; ii++) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(count_out,
                                    *((uint32_t*) batch_out->data + ii),
                                    "Expected linear increase");
      count_out++;
    }
    CHECK_ERR(bb_del_tail(&output));
  }
}

void test_cascading_complete(void)
{
  Bp_EC err;
  TEST_MESSAGE("Filter cascade entry");

  /* Setup */
  count_out = 0;
  count_in = 0;
  CHECK_ERR(filt_sink_connect(&filt1, 0, &filt2.input_buffers[0]));
  CHECK_ERR(filt_sink_connect(&filt2, 0, &filt3.input_buffers[0]));
  CHECK_ERR(filt_sink_connect(&filt3, 0, &output));
  CHECK_ERR(filt_start(&filt1));
  CHECK_ERR(filt_start(&filt2));
  CHECK_ERR(filt_start(&filt3));
  CHECK_ERR(bb_start(&output));
  CHECK_ERR(filt1.worker_err_info.ec);
  CHECK_ERR(filt2.worker_err_info.ec);
  CHECK_ERR(filt3.worker_err_info.ec);

  for (int i = 0; i < 3; i++) {
    CHECK_ERR(bb_submit(&filt1.input_buffers[0], 1000));
  }

  batch_in = bb_get_head(&filt1.input_buffers[0]);
  batch_in->ec = Bp_EC_COMPLETE;
  CHECK_ERR(bb_submit(&filt1.input_buffers[0], 1000));

  for (int i = 0; i < 3; i++) {
    batch_out = bb_get_tail(&output, 1000, &err);
    CHECK_ERR(err);
    CHECK_ERR(bb_del_tail(&output));
  }
  batch_out = bb_get_tail(&output, 1000, &err);
  CHECK_ERR(err);

  /* check that the sentinal batch has arrived */
  TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_COMPLETE, batch_out->ec,
                                "Expected completed batch");
  TEST_ASSERT_EQUAL_INT_MESSAGE(false, filt1.running,
                                "Filter should be stopped by sentinel");
  TEST_ASSERT_EQUAL_INT_MESSAGE(false, filt2.running,
                                "Filter should be stopped by sentinel");
  TEST_ASSERT_EQUAL_INT_MESSAGE(false, filt3.running,
                                "Filter should be stopped by sentinel");
}

void test_shutdown_with_data(void) {}

int main(int argc, char* argv[])
{
  TEST_MESSAGE("Test core filter.");
  UNITY_BEGIN();
  RUN_TEST(test_init_and_teardown);
  RUN_TEST(test_data_passthrough_single_thread);
  RUN_TEST(test_filter_cascade);
  RUN_TEST(test_cascading_complete);
  return UNITY_END();
}
