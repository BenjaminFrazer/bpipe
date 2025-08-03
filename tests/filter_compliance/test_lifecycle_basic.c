/**
 * @file test_lifecycle_basic.c
 * @brief Basic lifecycle compliance test
 */

#include "common.h"

void test_lifecycle_basic(void)
{
  TEST_ASSERT_NOT_NULL(g_fut);

  // Test init
  ASSERT_BP_OK(g_fut_init(g_fut, g_fut_config));
  TEST_ASSERT_TRUE_MESSAGE(g_fut->name[0] != '\0',
                           "Filter name not set after init");

  // Test that we can't double-init
  Bp_EC err = g_fut_init(g_fut, g_fut_config);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(Bp_EC_OK, err,
                                "Filter should reject double init");

  // Test deinit
  ASSERT_DEINIT_OK(g_fut);
}