#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include "unity.h"
#include "bperr.h"

/* Common test macro for checking error codes */
#define CHECK_ERR(ERR)                                                  \
  do {                                                                  \
    Bp_EC _ec = ERR;                                                    \
    TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, _ec, "Error in operation"); \
  } while (false)

#endif /* TEST_UTILS_H */