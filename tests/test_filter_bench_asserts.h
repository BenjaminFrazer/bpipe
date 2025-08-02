/**
 * @file test_filter_bench_asserts.h
 * @brief Custom assertion macros for filter bench testing with enhanced error context
 */

#ifndef TEST_FILTER_BENCH_ASSERTS_H
#define TEST_FILTER_BENCH_ASSERTS_H

#include "unity.h"
#include "bperr.h"
#include <stdio.h>

// Macro to check Bp_EC errors with automatic context
#define ASSERT_BP_OK(expr) do { \
    Bp_EC _ec = (expr); \
    if (_ec != Bp_EC_OK) { \
        char _msg[512]; \
        snprintf(_msg, sizeof(_msg), \
                 "%s failed at %s:%d - %s (EC: %d)", \
                 #expr, __FILE__, __LINE__, \
                 (_ec < Bp_EC_MAX) ? err_lut[_ec] : "Unknown error", _ec); \
        TEST_FAIL_MESSAGE(_msg); \
    } \
} while(0)

// Assert with custom context message
#define ASSERT_BP_OK_CTX(expr, ctx_fmt, ...) do { \
    Bp_EC _ec = (expr); \
    if (_ec != Bp_EC_OK) { \
        char _ctx[256]; \
        char _msg[512]; \
        snprintf(_ctx, sizeof(_ctx), ctx_fmt, ##__VA_ARGS__); \
        snprintf(_msg, sizeof(_msg), \
                 "%s - %s (EC: %d) at %s:%d", \
                 _ctx, (_ec < Bp_EC_MAX) ? err_lut[_ec] : "Unknown", \
                 _ec, __FILE__, __LINE__); \
        TEST_FAIL_MESSAGE(_msg); \
    } \
} while(0)

// Check worker error with full context
#define ASSERT_WORKER_OK(filter) do { \
    if ((filter)->worker_err_info.ec != Bp_EC_OK) { \
        char _msg[768]; \
        snprintf(_msg, sizeof(_msg), \
                 "Worker error in %s at %s:%d - %s (EC: %d) | Filter: %s", \
                 (filter)->worker_err_info.function ?: "unknown", \
                 (filter)->worker_err_info.filename ?: "unknown", \
                 (filter)->worker_err_info.line_no, \
                 ((filter)->worker_err_info.ec < Bp_EC_MAX) ? \
                     err_lut[(filter)->worker_err_info.ec] : "Unknown", \
                 (filter)->worker_err_info.ec, \
                 (filter)->name); \
        if ((filter)->worker_err_info.err_msg) { \
            strncat(_msg, " | Msg: ", sizeof(_msg) - strlen(_msg) - 1); \
            strncat(_msg, (filter)->worker_err_info.err_msg, \
                    sizeof(_msg) - strlen(_msg) - 1); \
        } \
        TEST_FAIL_MESSAGE(_msg); \
    } \
} while(0)

// Memory allocation with context
#define ASSERT_ALLOC(ptr, desc) do { \
    if (!(ptr)) { \
        char _msg[256]; \
        snprintf(_msg, sizeof(_msg), \
                 "Failed to allocate %s at %s:%d", \
                 desc, __FILE__, __LINE__); \
        TEST_FAIL_MESSAGE(_msg); \
    } \
} while(0)

// Memory allocation with size info
#define ASSERT_ALLOC_SIZE(ptr, size, desc) do { \
    if (!(ptr)) { \
        char _msg[256]; \
        snprintf(_msg, sizeof(_msg), \
                 "Failed to allocate %s (size: %zu bytes) at %s:%d", \
                 desc, (size_t)(size), __FILE__, __LINE__); \
        TEST_FAIL_MESSAGE(_msg); \
    } \
} while(0)

// Array allocation with index context
#define ASSERT_ALLOC_ARRAY(ptr, idx, total, desc) do { \
    if (!(ptr)) { \
        char _msg[256]; \
        snprintf(_msg, sizeof(_msg), \
                 "Failed to allocate %s[%d] of %d at %s:%d", \
                 desc, idx, total, __FILE__, __LINE__); \
        TEST_FAIL_MESSAGE(_msg); \
    } \
} while(0)

// Connection assertions
#define ASSERT_CONNECT_OK(src_filter, src_idx, dst_buffer) do { \
    Bp_EC _ec = filt_sink_connect(src_filter, src_idx, dst_buffer); \
    if (_ec != Bp_EC_OK) { \
        char _msg[512]; \
        snprintf(_msg, sizeof(_msg), \
                 "Failed to connect %s output[%zu] to buffer - %s (EC: %d)", \
                 (src_filter)->name, (size_t)(src_idx), \
                 (_ec < Bp_EC_MAX) ? err_lut[_ec] : "Unknown", _ec); \
        TEST_FAIL_MESSAGE(_msg); \
    } \
} while(0)

// Producer to filter connection
#define ASSERT_PRODUCER_CONNECT(prod_idx, prod, filter, input_idx) do { \
    Bp_EC _ec = filt_sink_connect(&(prod)->base, 0, (filter)->input_buffers[input_idx]); \
    if (_ec != Bp_EC_OK) { \
        char _msg[512]; \
        snprintf(_msg, sizeof(_msg), \
                 "Failed to connect producer[%d] '%s' to %s input[%d] - %s (EC: %d)", \
                 prod_idx, (prod)->base.name, (filter)->name, input_idx, \
                 (_ec < Bp_EC_MAX) ? err_lut[_ec] : "Unknown", _ec); \
        TEST_FAIL_MESSAGE(_msg); \
    } \
} while(0)

// Lifecycle assertions
#define ASSERT_INIT_OK(filter, config) \
    ASSERT_BP_OK_CTX(g_fut_init(filter, config), \
                     "Failed to initialize filter %s", (filter)->name)

#define ASSERT_START_OK(filter) \
    ASSERT_BP_OK_CTX(filt_start(filter), \
                     "Failed to start filter %s", (filter)->name)

#define ASSERT_STOP_OK(filter) \
    ASSERT_BP_OK_CTX(filt_stop(filter), \
                     "Failed to stop filter %s", (filter)->name)

#define ASSERT_DEINIT_OK(filter) \
    ASSERT_BP_OK_CTX(filt_deinit(filter), \
                     "Failed to deinit filter %s", (filter)->name)

// Timing/sequence validation assertions
#define ASSERT_NO_SEQ_ERRORS(consumer) do { \
    size_t _errors = atomic_load(&(consumer)->sequence_errors); \
    if (_errors > 0) { \
        char _msg[256]; \
        snprintf(_msg, sizeof(_msg), \
                 "Consumer '%s' detected %zu sequence errors", \
                 (consumer)->base.name, _errors); \
        TEST_FAIL_MESSAGE(_msg); \
    } \
} while(0)

#define ASSERT_NO_TIMING_ERRORS(consumer) do { \
    size_t _errors = atomic_load(&(consumer)->timing_errors); \
    if (_errors > 0) { \
        char _msg[256]; \
        snprintf(_msg, sizeof(_msg), \
                 "Consumer '%s' detected %zu timing errors", \
                 (consumer)->base.name, _errors); \
        TEST_FAIL_MESSAGE(_msg); \
    } \
} while(0)

// Data flow assertions
#define ASSERT_BATCHES_CONSUMED(consumer, expected, tolerance) do { \
    size_t _actual = atomic_load(&(consumer)->batches_consumed); \
    if (abs((int)_actual - (int)(expected)) > (tolerance)) { \
        char _msg[256]; \
        snprintf(_msg, sizeof(_msg), \
                 "Consumer '%s' consumed %zu batches, expected %zu ± %d", \
                 (consumer)->base.name, _actual, (size_t)(expected), (tolerance)); \
        TEST_FAIL_MESSAGE(_msg); \
    } \
} while(0)

#define ASSERT_BATCHES_PRODUCED(producer, expected) do { \
    size_t _actual = atomic_load(&(producer)->batches_produced); \
    if (_actual != (expected)) { \
        char _msg[256]; \
        snprintf(_msg, sizeof(_msg), \
                 "Producer '%s' produced %zu batches, expected %zu", \
                 (producer)->base.name, _actual, (size_t)(expected)); \
        TEST_FAIL_MESSAGE(_msg); \
    } \
} while(0)

#endif // TEST_FILTER_BENCH_ASSERTS_H