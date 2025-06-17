#include "../bpipe/core.h"
#include "unity.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>

/* Helper to create sawtooth data */
static void fill_sawtooth(unsigned *buf, size_t len)
{
    for(size_t i=0;i<len;i++)
        buf[i] = (unsigned)(i & 0xFFu);
}

static void init_filter(Bp_Filter_t* f)
{
    memset(f, 0, sizeof(*f));
    f->transform = BpPassThroughTransform;
    f->ring_capacity_expo = 4;
    /* User requirement states batch_capacity_expo=8 with capacity 64. */
    /* To match capacity 64 we use exponent 6. */
    f->batch_capacity_expo = 6;
    f->dtype = DTYPE_UNSIGNED;
    f->data_width = sizeof(unsigned);
    f->modulo_mask = (1u << f->ring_capacity_expo) - 1u;
    Bp_allocate_buffers(f);
}

static void free_filter(Bp_Filter_t* f)
{
    Bp_deallocate_buffers(f);
}

static void test_sawtooth_full(void)
{
    Bp_Filter_t filt;
    init_filter(&filt);

    const size_t samples = (1u<<6) * (1u<<4) * 10u; /* 64 * 16 * 10 */

    unsigned *input = malloc(sizeof(unsigned)*samples);
    unsigned *output = calloc(samples, sizeof(unsigned));
    fill_sawtooth(input, samples);

    Bp_Batch_t in_batch = {
        .head = samples,
        .tail = 0,
        .capacity = samples,
        .t_ns = 0,
        .period_ns = 1,
        .dtype = DTYPE_UNSIGNED,
        .data = input
    };
    Bp_Batch_t out_batch = {
        .head = 0,
        .tail = 0,
        .capacity = samples,
        .t_ns = 0,
        .period_ns = 0,
        .dtype = DTYPE_UNSIGNED,
        .data = output
    };

    struct timespec ts0, ts1;
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    filt.transform(&filt, &in_batch, &out_batch);
    clock_gettime(CLOCK_MONOTONIC, &ts1);

    long long dt_ns = (long long)(ts1.tv_sec - ts0.tv_sec)*1000000000ll +
                       (ts1.tv_nsec - ts0.tv_nsec);

    TEST_ASSERT_EQUAL_UINT(samples, out_batch.head);
    TEST_ASSERT_EQUAL_UINT(samples, in_batch.tail);
    TEST_ASSERT_EQUAL_UINT(in_batch.t_ns, out_batch.t_ns);
    TEST_ASSERT_EQUAL_UINT(in_batch.period_ns, out_batch.period_ns);
    TEST_ASSERT_EQUAL_UINT_ARRAY(input, output, samples);
    TEST_ASSERT_TRUE(dt_ns < 1000000ll); /* <1ms */

    free(input);
    free(output);
    free_filter(&filt);
}

static void test_sawtooth_partial(void)
{
    Bp_Filter_t filt;
    init_filter(&filt);

    const size_t samples = 32u;

    unsigned *input = malloc(sizeof(unsigned)*samples);
    unsigned *output = calloc(samples, sizeof(unsigned));
    fill_sawtooth(input, samples);

    Bp_Batch_t in_batch = {
        .head = samples,
        .tail = 0,
        .capacity = samples,
        .t_ns = 0,
        .period_ns = 1,
        .dtype = DTYPE_UNSIGNED,
        .data = input
    };
    Bp_Batch_t out_batch = {
        .head = 0,
        .tail = 0,
        .capacity = samples,
        .t_ns = 0,
        .period_ns = 0,
        .dtype = DTYPE_UNSIGNED,
        .data = output
    };

    struct timespec ts0, ts1;
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    filt.transform(&filt, &in_batch, &out_batch);
    clock_gettime(CLOCK_MONOTONIC, &ts1);

    long long dt_ns = (long long)(ts1.tv_sec - ts0.tv_sec)*1000000000ll +
                       (ts1.tv_nsec - ts0.tv_nsec);

    TEST_ASSERT_EQUAL_UINT(samples, out_batch.head);
    TEST_ASSERT_EQUAL_UINT(samples, in_batch.tail);
    TEST_ASSERT_EQUAL_UINT(in_batch.t_ns, out_batch.t_ns);
    TEST_ASSERT_EQUAL_UINT(in_batch.period_ns, out_batch.period_ns);
    TEST_ASSERT_EQUAL_UINT_ARRAY(input, output, samples);
    TEST_ASSERT_TRUE(dt_ns < 1000000ll); /* <1ms */

    free(input);
    free(output);
    free_filter(&filt);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sawtooth_full);
    RUN_TEST(test_sawtooth_partial);
    return UNITY_END();
}

