#include "../bpipe/core.h"
#include "unity.h"
#include <pthread.h>
#include <string.h>

static void init_filter(Bp_Filter_t* f)
{
    memset(f, 0, sizeof(*f));
    f->transform = BpPassThroughTransform;
    f->buffer.ring_capacity_expo = 2;
    f->buffer.batch_capacity_expo = 2;
    f->dtype = DTYPE_UNSIGNED;
    f->data_width = sizeof(unsigned);
    f->has_input_buffer = true;
    f->timeout.tv_sec = 1;
    f->timeout.tv_nsec = 0;
    pthread_mutex_init(&f->buffer.mutex, NULL);
    pthread_cond_init(&f->buffer.not_full, NULL);
    pthread_cond_init(&f->buffer.not_empty, NULL);
    Bp_allocate_buffers(f);
}

static void free_filter(Bp_Filter_t* f)
{
    Bp_deallocate_buffers(f);
}

static void test_sentinel_propagation(void)
{
    Bp_Filter_t a, b;
    init_filter(&a);
    init_filter(&b);

    a.sink = &b;

    a.running = true;
    b.running = true;

    pthread_create(&b.worker_thread, NULL, Bp_Worker, &b);
    pthread_create(&a.worker_thread, NULL, Bp_Worker, &a);

    Bp_Batch_t done = { .ec = Bp_EC_COMPLETE };
    Bp_submit_batch(&a, &a.buffer, &done);

    pthread_join(a.worker_thread, NULL);
    pthread_join(b.worker_thread, NULL);

    TEST_ASSERT_TRUE(!a.running);
    TEST_ASSERT_TRUE(!b.running);

    Bp_Batch_t rx = Bp_head(&b, &b.buffer);
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
