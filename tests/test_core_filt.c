#include <bits/pthreadtypes.h>
#include <bits/time.h>
#include <bits/types/struct_iovec.h>
#include <bits/types/timer_t.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include "unity_internals.h"
#define _DEFAULT_SOURCE
#include <unistd.h>
#include "../bpipe/batch_buffer.h"
#include "../bpipe/core.h"
#include "time.h"
#include "unity.h"

#define BATCH_CAPACITY_EXPO 4
#define RING_CAPACITY_EXPO 4

char buff[124];

size_t ring_capacity = (1 << RING_CAPACITY_EXPO) -
                       1;  // TODO: check this, due to the "always occupied slot
size_t batch_capacity = (1 << BATCH_CAPACITY_EXPO);  //

Filter_t filt;
Batch_buff_t output;

struct timespec ts_1ms = {.tv_nsec = 1000000};  // 10ms

Bp_EC _ec;

#define CHECK_ERR(ERR)                                          \
  do {                                                          \
    _ec = ERR;                                                  \
    TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, _ec, err_lut[_ec]); \
  } while (false);

void setUp(void)
{
  TEST_MESSAGE("setUp entry");
  BatchBuffer_config config = {.dtype = DTYPE_U32,
                               .overflow_behaviour = OVERFLOW_BLOCK,
                               .ring_capacity_expo = RING_CAPACITY_EXPO,
                               .batch_capacity_expo = BATCH_CAPACITY_EXPO};

  Core_filt_config_t filter_config = {
      .name = "TEST_FILTER",
      .size = sizeof(Filter_t),
      .worker = &matched_passthroug,
      .timeout_us = 1000,  // infinite timeout
      .max_supported_sinks = 1,
      .n_inputs = 1,
      .filt_type = FILT_T_MATCHED_PASSTHROUGH,
      .buff_config = config,
  };

  CHECK_ERR(bb_init(&output, "OUTPUT", config));
  CHECK_ERR(filt_init(&filt, filter_config));
  CHECK_ERR(filt_sink_connect(&filt, 0, &output));
  CHECK_ERR(bb_start(&output));
  CHECK_ERR(filt_start(&filt));
  CHECK_ERR(filt.worker_err_info.ec);
  TEST_MESSAGE("setUp exit");
}

void tearDown(void)
{
  TEST_MESSAGE("tearDown entry");
  CHECK_ERR(filt_stop(&filt));
  CHECK_ERR(bb_stop(&output));
  CHECK_ERR(bb_deinit(&output));
  CHECK_ERR(filt_sink_disconnect(&filt, 0));
  CHECK_ERR(filt_deinit(&filt));
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
  for (int i = 0; i < ring_capacity; i++) {
    batch_in = bb_get_head(&filt.input_buffers[0]);
    for (int ii = 0; ii < batch_capacity; ii++) {
      *((uint32_t*) batch_in->data + ii) = count_in;
      count_in++;
    }
    // TEST_MESSAGE("Submitting batch to input");
    CHECK_ERR(
        bb_submit(&filt.input_buffers[0], 1000));  // should always be space
    // wait a bit and check if there has been a worker error
    nanosleep(&ts_1ms, NULL);
    CHECK_ERR(filt.worker_err_info.ec);  //
    // TEST_MESSAGE("Getting output tail");
    //  TEST_MESSAGE("Input");
    //  bb_print(&filt.input_buffers[0]);
    //  TEST_MESSAGE("Output");
    //  bb_print(&output);
    batch_out = bb_get_tail(&output, 1000, &err);
    CHECK_ERR(err);  // should always be space
    for (int ii = 0; ii < batch_capacity; ii++) {
      uint32_t out = *((uint32_t*) batch_in->data + ii);
      TEST_ASSERT_EQUAL_INT_MESSAGE(
          count_out, out, "Output data is not incrementing linearly.");
      count_out++;
    }
    CHECK_ERR(bb_del_tail(&output));
  }
  TEST_MESSAGE("Data passthrough exit");
}

void test_shutdown_with_data(void) {}

int main(int argc, char* argv[])
{
  TEST_MESSAGE("Test core filter.");
  UNITY_BEGIN();
  RUN_TEST(test_init_and_teardown);
  RUN_TEST(test_data_passthrough_single_thread);
  return UNITY_END();
}
