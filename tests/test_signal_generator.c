#include <math.h>
#include <string.h>
#include "resampler.h"
#include "signal_gen.h"
#inlcude "test_utils.h"
#include "unity.h"

/* Test fixtures */
static Bp_SignalGen_t source3;
static Bp_Filter_t sink;

/* Test parameters */
#define TEST_BATCH_SIZE 64
#define TEST_BUFFER_SIZE 128
#define TEST_N_BATCHES_EXP 6

void setUp(void)
{
  memset(&resampler, 0, sizeof(resampler));
  memset(&source1, 0, sizeof(source1));
  memset(&source2, 0, sizeof(source2));
  memset(&source3, 0, sizeof(source3));
  memset(&sink, 0, sizeof(sink));
}

void tearDown(void)
{ /* Cleanup is handled by individual tests */
}

/* Main test runner */
int main(void)
{
  UNITY_BEGIN();

  /* Initialization tests */
  RUN_TEST(test_resampler_init_valid);

  return UNITY_END();
}
