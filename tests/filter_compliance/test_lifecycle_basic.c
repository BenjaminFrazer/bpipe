/**
 * @file test_lifecycle_basic.c
 * @brief Basic lifecycle compliance test
 */

#include "common.h"

/**
 * Intent: Verify basic filter lifecycle operations work correctly
 * (init → deinit sequence).
 * 
 * Approach:
 * 1. Initialize filter with default configuration
 * 2. Verify filter name is set after initialization
 * 3. Attempt to double-initialize (should fail)
 * 4. Deinitialize the filter
 * 
 * This is the most fundamental test - ensures filters can be
 * created and destroyed without errors or resource leaks.
 */
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