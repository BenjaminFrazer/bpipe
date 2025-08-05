/**
 * @file test_lifecycle_errors.c
 * @brief Test error handling in lifecycle operations
 */

#include "common.h"

void test_lifecycle_errors(void)
{
  // Test operations on uninitialized filter
  Bp_EC err = filt_start(g_fut);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(Bp_EC_OK, err,
                                "Should not start uninitialized filter");

  err = filt_stop(g_fut);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(Bp_EC_OK, err,
                                "Should not stop uninitialized filter");

  err = filt_deinit(g_fut);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(Bp_EC_OK, err,
                                "Should not deinit uninitialized filter");
}