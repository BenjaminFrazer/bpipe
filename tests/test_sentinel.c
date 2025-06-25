#include "../bpipe/core.h"
#include "unity.h"
#include <pthread.h>
#include <string.h>

static void init_filter(Bp_Filter_t* f)
{
    memset(f, 0, sizeof(*f));
    f->transform = BpPassThroughTransform;
    f->input_buffers[0].ring_capacity_expo = 2;
    f->input_buffers[0].batch_capacity_expo = 2;
    f->dtype = DTYPE_UNSIGNED;
    f->data_width = sizeof(unsigned);
    // Input buffer will be allocated and used based on buffer initialization
    f->timeout.tv_sec = 1;
    f->timeout.tv_nsec = 0;
    pthread_mutex_init(&f->input_buffers[0].mutex, NULL);
    pthread_cond_init(&f->input_buffers[0].not_full, NULL);
    pthread_cond_init(&f->input_buffers[0].not_empty, NULL);
    Bp_allocate_buffers(f, 0);
}

static void free_filter(Bp_Filter_t* f)
{
    Bp_deallocate_buffers(f, 0);
}

void setUp(void) {
    // Setup code for each test
}

void tearDown(void) {
    // Cleanup code for each test
}

static void test_sentinel_propagation(void)
{
    Bp_Filter_t a, b;
    init_filter(&a);
    init_filter(&b);

    Bp_add_sink(&a, &b);

    Bp_Filter_Start(&b);
    Bp_Filter_Start(&a);

    Bp_Batch_t done = { .ec = Bp_EC_COMPLETE };
    Bp_submit_batch(&a, &a.input_buffers[0], &done);

    Bp_Filter_Stop(&a);
    Bp_Filter_Stop(&b);

    TEST_ASSERT_TRUE(!a.running);
    TEST_ASSERT_TRUE(!b.running);

    Bp_Batch_t rx = Bp_head(&b, &b.input_buffers[0]);
    TEST_ASSERT_EQUAL_UINT(Bp_EC_COMPLETE, rx.ec);

    free_filter(&a);
    free_filter(&b);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sentinel_propagation);
    return UNITY_END();
}
