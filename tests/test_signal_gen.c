#include "../bpipe/signal_gen.h"
#include "unity.h"
#include <stdlib.h>
#include <string.h>

static void init_pass_through(Bp_Filter_t* f)
{
    memset(f, 0, sizeof(*f));
    f->transform = BpPassThroughTransform;
    f->buffer.ring_capacity_expo = 4;
    f->buffer.batch_capacity_expo = 6;
    f->dtype = DTYPE_UNSIGNED;
    f->data_width = sizeof(unsigned);
    f->has_input_buffer = true;
    pthread_mutex_init(&f->buffer.mutex, NULL);
    pthread_cond_init(&f->buffer.not_full, NULL);
    pthread_cond_init(&f->buffer.not_empty, NULL);
    Bp_allocate_buffers(f);
}

static void free_filter(Bp_Filter_t* f)
{
    Bp_deallocate_buffers(f);
}

static void test_generator_sawtooth(void)
{
    Bp_Filter_t sink;
    init_pass_through(&sink);

    Bp_SignalGen_t gen;
    memset(&gen, 0, sizeof(gen));
    gen.base.transform = BpSignalGenTransform;
    gen.base.dtype = DTYPE_UNSIGNED;
    gen.base.data_width = sizeof(unsigned);
    gen.waveform = BP_WAVE_SAWTOOTH;
    gen.amplitude = 256.0f;
    gen.frequency = 1.0f/256.0f; /* step of 1 per sample */
    gen.phase = 0.0f;
    gen.x_offset = 0.0f;

    unsigned out_buf[10] = {0};
    Bp_Batch_t out_batch = {
        .head = 0,
        .tail = 0,
        .capacity = 10,
        .t_ns = 0,
        .period_ns = 1000000u,
        .dtype = DTYPE_UNSIGNED,
        .data = out_buf
    };

    gen.base.transform((Bp_Filter_t*)&gen, &(Bp_Batch_t){0}, &out_batch);

    unsigned exp[10];
    for(unsigned i=0;i<10;i++)
        exp[i] = i;

    TEST_ASSERT_EQUAL_UINT(10, out_batch.head);
    TEST_ASSERT_EQUAL_UINT_ARRAY(exp, out_buf, 10);

    free_filter(&sink);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_generator_sawtooth);
    return UNITY_END();
}
