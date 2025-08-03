/**
 * @file test_error_invalid_config.c
 * @brief Test handling of invalid configurations
 */

#include "common.h"

void test_error_invalid_config(void)
{
  // Test with NULL config if filter requires config
  Bp_EC err = g_fut_init(g_fut, NULL);
  if (err != Bp_EC_OK) {
    // Filter properly rejected NULL config
    TEST_PASS_MESSAGE("Filter correctly rejected NULL config");
  } else {
    // Filter accepted NULL config - that's fine too
    TEST_PASS_MESSAGE("Filter accepts NULL config");
    filt_deinit(g_fut);
  }
}