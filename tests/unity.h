#ifndef UNITY_H
#define UNITY_H
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

static int UnityFailCount = 0;
static int UnityTestCount = 0;

#define UNITY_BEGIN() do { UnityFailCount = 0; UnityTestCount = 0; } while(0)
#define UNITY_END() (UnityFailCount)

#define RUN_TEST(func) do { \
    UnityTestCount++; \
    printf("RUN_TEST: %s\n", #func); \
    func(); \
} while(0)

#define TEST_ASSERT_TRUE(cond) do { \
    if(!(cond)) { \
        printf("Assertion failed: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        UnityFailCount++; \
    } \
} while(0)

#define TEST_ASSERT_EQUAL_UINT(expected, actual) do { \
    if((unsigned)(expected) != (unsigned)(actual)) { \
        printf("Assertion failed: %s:%d Expected %u Got %u\n", __FILE__, __LINE__, (unsigned)(expected), (unsigned)(actual)); \
        UnityFailCount++; \
    } \
} while(0)

#define TEST_ASSERT_EQUAL_UINT_ARRAY(exp, act, len) do { \
    for(size_t _i = 0; _i < (len); ++_i) { \
        if(((unsigned*)(exp))[_i] != ((unsigned*)(act))[_i]) { \
            printf("Assertion failed: %s:%d idx %zu Expected %u Got %u\n", __FILE__, __LINE__, _i, ((unsigned*)(exp))[_i], ((unsigned*)(act))[_i]); \
            UnityFailCount++; \
            break; \
        } \
    } \
} while(0)

#endif // UNITY_H
