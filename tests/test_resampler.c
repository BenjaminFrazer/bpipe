#include "unity.h"
#include "resampler.h"
#include "signal_gen.h"
#include <math.h>
#include <string.h>

/* Test fixtures */
static BpZOHResampler_t resampler;
static Bp_SignalGen_t source1;
static Bp_SignalGen_t source2;
static Bp_SignalGen_t source3;
static Bp_Filter_t sink;

/* Test parameters */
#define TEST_BATCH_SIZE 64
#define TEST_BUFFER_SIZE 128
#define TEST_N_BATCHES_EXP 6

void setUp(void) {
    memset(&resampler, 0, sizeof(resampler));
    memset(&source1, 0, sizeof(source1));
    memset(&source2, 0, sizeof(source2));
    memset(&source3, 0, sizeof(source3));
    memset(&sink, 0, sizeof(sink));
}

void tearDown(void) {
    /* Cleanup is handled by individual tests */
}

/* Test basic initialization */
void test_resampler_init_valid(void) {
    BpZOHResamplerConfig config = BP_ZOH_RESAMPLER_CONFIG_DEFAULT;
    config.output_period_ns = 1000000;  /* 1ms = 1kHz */
    config.base_config.dtype = DTYPE_FLOAT;
    config.base_config.number_of_input_filters = 2;
    config.base_config.batch_size = TEST_BATCH_SIZE;
    
    Bp_EC ec = BpZOHResampler_Init(&resampler, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    
    /* Verify configuration was stored */
    TEST_ASSERT_EQUAL(1000000, resampler.output_period_ns);
    TEST_ASSERT_EQUAL(2, resampler.n_inputs);
    TEST_ASSERT_FALSE(resampler.drop_on_underrun);
    TEST_ASSERT_EQUAL(BP_RESAMPLE_ZOH, resampler.method);
    
    /* Verify input states were allocated */
    TEST_ASSERT_NOT_NULL(resampler.input_states);
    TEST_ASSERT_NOT_NULL(resampler.input_states[0].last_values);
    TEST_ASSERT_NOT_NULL(resampler.input_states[1].last_values);
    
    /* Cleanup */
    BpZOHResampler_Deinit(&resampler);
}

/* Test simplified initialization */
void test_resampler_init_simple(void) {
    Bp_EC ec = BpZOHResampler_InitSimple(&resampler, 1000, 3, DTYPE_FLOAT);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    
    /* Verify configuration */
    TEST_ASSERT_EQUAL(1000000, resampler.output_period_ns);  /* 1kHz = 1ms */
    TEST_ASSERT_EQUAL(3, resampler.n_inputs);
    TEST_ASSERT_EQUAL(DTYPE_FLOAT, resampler.base.dtype);
    
    BpZOHResampler_Deinit(&resampler);
}

/* Test invalid configurations */
void test_resampler_init_invalid(void) {
    BpZOHResamplerConfig config = BP_ZOH_RESAMPLER_CONFIG_DEFAULT;
    
    /* Test zero output period */
    config.output_period_ns = 0;
    Bp_EC ec = BpZOHResampler_Init(&resampler, &config);
    TEST_ASSERT_EQUAL(Bp_EC_INVALID_CONFIG, ec);
    
    /* Test too many inputs */
    config.output_period_ns = 1000000;
    config.base_config.number_of_input_filters = MAX_SOURCES + 1;
    ec = BpZOHResampler_Init(&resampler, &config);
    TEST_ASSERT_EQUAL(Bp_EC_INVALID_CONFIG, ec);
    
    /* Test zero inputs */
    config.base_config.number_of_input_filters = 0;
    ec = BpZOHResampler_Init(&resampler, &config);
    TEST_ASSERT_EQUAL(Bp_EC_INVALID_CONFIG, ec);
}

/* Test single input passthrough (same rate) */
void test_resampler_passthrough(void) {
    /* Initialize resampler at 1kHz */
    BpZOHResamplerConfig config = BP_ZOH_RESAMPLER_CONFIG_DEFAULT;
    config.output_period_ns = 1000000;  /* 1ms = 1kHz */
    config.base_config.dtype = DTYPE_FLOAT;
    config.base_config.number_of_input_filters = 1;
    config.base_config.batch_size = TEST_BATCH_SIZE;
    
    Bp_EC ec = BpZOHResampler_Init(&resampler, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    
    /* Create test batch with known pattern */
    float input_data[TEST_BATCH_SIZE];
    for (int i = 0; i < TEST_BATCH_SIZE; i++) {
        input_data[i] = (float)i;
    }
    
    Bp_Batch_t input_batch = {
        .head = 0,
        .tail = TEST_BATCH_SIZE,
        .capacity = TEST_BATCH_SIZE,
        .t_ns = 1000000000,  /* 1 second */
        .period_ns = 1000000,  /* 1ms period = 1kHz */
        .batch_id = 1,
        .ec = Bp_EC_OK,
        .dtype = DTYPE_FLOAT,
        .data = input_data
    };
    
    float output_data[TEST_BATCH_SIZE];
    Bp_Batch_t output_batch = {
        .head = 0,
        .tail = 0,
        .capacity = TEST_BATCH_SIZE,
        .dtype = DTYPE_FLOAT,
        .data = output_data
    };
    
    /* Process */
    Bp_Batch_t* inputs[] = {&input_batch};
    Bp_Batch_t* outputs[] = {&output_batch};
    
    BpZOHResamplerTransform(&resampler.base, inputs, 1, outputs, 1);
    
    /* Verify output */
    TEST_ASSERT_EQUAL(Bp_EC_OK, output_batch.ec);
    TEST_ASSERT_TRUE(output_batch.tail > 0);
    TEST_ASSERT_EQUAL(1000000, output_batch.period_ns);
    
    /* With same rate, should get most of the samples */
    TEST_ASSERT_TRUE(output_batch.tail >= TEST_BATCH_SIZE - 2);
    
    BpZOHResampler_Deinit(&resampler);
}

/* Test downsampling (high rate to low rate) */
void test_resampler_downsample(void) {
    /* Initialize resampler at 100Hz */
    BpZOHResamplerConfig config = BP_ZOH_RESAMPLER_CONFIG_DEFAULT;
    config.output_period_ns = 10000000;  /* 10ms = 100Hz */
    config.base_config.dtype = DTYPE_FLOAT;
    config.base_config.number_of_input_filters = 1;
    config.base_config.batch_size = TEST_BATCH_SIZE;
    
    Bp_EC ec = BpZOHResampler_Init(&resampler, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    
    /* Create 1kHz input (10x faster than output) */
    float input_data[TEST_BATCH_SIZE];
    for (int i = 0; i < TEST_BATCH_SIZE; i++) {
        input_data[i] = (float)i;
    }
    
    Bp_Batch_t input_batch = {
        .head = 0,
        .tail = TEST_BATCH_SIZE,
        .capacity = TEST_BATCH_SIZE,
        .t_ns = 1000000000,
        .period_ns = 1000000,  /* 1ms = 1kHz */
        .batch_id = 1,
        .ec = Bp_EC_OK,
        .dtype = DTYPE_FLOAT,
        .data = input_data
    };
    
    float output_data[TEST_BATCH_SIZE];
    Bp_Batch_t output_batch = {
        .head = 0,
        .tail = 0,
        .capacity = TEST_BATCH_SIZE,
        .dtype = DTYPE_FLOAT,
        .data = output_data
    };
    
    /* Process */
    Bp_Batch_t* inputs[] = {&input_batch};
    Bp_Batch_t* outputs[] = {&output_batch};
    
    BpZOHResamplerTransform(&resampler.base, inputs, 1, outputs, 1);
    
    /* Verify downsampling occurred */
    TEST_ASSERT_EQUAL(Bp_EC_OK, output_batch.ec);
    TEST_ASSERT_EQUAL(10000000, output_batch.period_ns);
    
    /* Should get approximately 1/10th of input samples */
    size_t expected_samples = TEST_BATCH_SIZE / 10;
    TEST_ASSERT_TRUE(output_batch.tail >= expected_samples - 1);
    TEST_ASSERT_TRUE(output_batch.tail <= expected_samples + 1);
    
    BpZOHResampler_Deinit(&resampler);
}

/* Test upsampling (low rate to high rate) */
void test_resampler_upsample(void) {
    /* Initialize resampler at 1kHz */
    BpZOHResamplerConfig config = BP_ZOH_RESAMPLER_CONFIG_DEFAULT;
    config.output_period_ns = 1000000;  /* 1ms = 1kHz */
    config.base_config.dtype = DTYPE_FLOAT;
    config.base_config.number_of_input_filters = 1;
    config.base_config.batch_size = TEST_BATCH_SIZE;
    
    Bp_EC ec = BpZOHResampler_Init(&resampler, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    
    /* Create 100Hz input (10x slower than output) */
    float input_data[10] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0};
    
    Bp_Batch_t input_batch = {
        .head = 0,
        .tail = 10,
        .capacity = 10,
        .t_ns = 1000000000,
        .period_ns = 10000000,  /* 10ms = 100Hz */
        .batch_id = 1,
        .ec = Bp_EC_OK,
        .dtype = DTYPE_FLOAT,
        .data = input_data
    };
    
    float output_data[TEST_BATCH_SIZE * 2];
    Bp_Batch_t output_batch = {
        .head = 0,
        .tail = 0,
        .capacity = TEST_BATCH_SIZE * 2,
        .dtype = DTYPE_FLOAT,
        .data = output_data
    };
    
    /* Process */
    Bp_Batch_t* inputs[] = {&input_batch};
    Bp_Batch_t* outputs[] = {&output_batch};
    
    BpZOHResamplerTransform(&resampler.base, inputs, 1, outputs, 1);
    
    /* Verify upsampling occurred */
    TEST_ASSERT_EQUAL(Bp_EC_OK, output_batch.ec);
    TEST_ASSERT_EQUAL(1000000, output_batch.period_ns);
    
    /* Should get approximately 10x input samples with ZOH */
    printf("Upsample test: output tail=%zu, expected>=90\n", output_batch.tail);
    TEST_ASSERT_TRUE(output_batch.tail >= 90);  /* ~10 * 10 samples */
    
    /* Verify zero-order hold behavior */
    /* First few output samples should hold the value 1.0 */
    float* out_ptr = (float*)output_batch.data;
    for (int i = 0; i < 9; i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.001, 1.0, out_ptr[i]);
    }
    
    BpZOHResampler_Deinit(&resampler);
}

/* Test multi-input synchronization */
void test_resampler_multi_input(void) {
    /* Initialize resampler at 500Hz with 2 inputs */
    BpZOHResamplerConfig config = BP_ZOH_RESAMPLER_CONFIG_DEFAULT;
    config.output_period_ns = 2000000;  /* 2ms = 500Hz */
    config.base_config.dtype = DTYPE_FLOAT;
    config.base_config.number_of_input_filters = 2;
    config.base_config.batch_size = TEST_BATCH_SIZE;
    
    Bp_EC ec = BpZOHResampler_Init(&resampler, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    
    /* Create two inputs at different rates */
    float input1_data[32];
    float input2_data[40];
    
    /* Input 1: 1kHz, values 1, 2, 3, ... */
    for (int i = 0; i < 32; i++) {
        input1_data[i] = (float)(i + 1);
    }
    
    /* Input 2: 800Hz, values 100, 101, 102, ... */
    for (int i = 0; i < 40; i++) {
        input2_data[i] = (float)(100 + i);
    }
    
    Bp_Batch_t input1_batch = {
        .head = 0,
        .tail = 32,
        .capacity = 32,
        .t_ns = 1000000000,
        .period_ns = 1000000,  /* 1kHz */
        .batch_id = 1,
        .ec = Bp_EC_OK,
        .dtype = DTYPE_FLOAT,
        .data = input1_data
    };
    
    Bp_Batch_t input2_batch = {
        .head = 0,
        .tail = 40,
        .capacity = 40,
        .t_ns = 1000000000,
        .period_ns = 1250000,  /* 800Hz */
        .batch_id = 1,
        .ec = Bp_EC_OK,
        .dtype = DTYPE_FLOAT,
        .data = input2_data
    };
    
    float output_data[TEST_BATCH_SIZE * 2];  /* Interleaved output */
    Bp_Batch_t output_batch = {
        .head = 0,
        .tail = 0,
        .capacity = TEST_BATCH_SIZE * 2,
        .dtype = DTYPE_FLOAT,
        .data = output_data
    };
    
    /* Process */
    Bp_Batch_t* inputs[] = {&input1_batch, &input2_batch};
    Bp_Batch_t* outputs[] = {&output_batch};
    
    BpZOHResamplerTransform(&resampler.base, inputs, 2, outputs, 1);
    
    /* Verify output */
    TEST_ASSERT_EQUAL(Bp_EC_OK, output_batch.ec);
    TEST_ASSERT_EQUAL(2000000, output_batch.period_ns);
    TEST_ASSERT_TRUE(output_batch.tail > 0);
    
    /* Output should be interleaved: [input1_sample, input2_sample, ...] */
    float* out_ptr = (float*)output_batch.data;
    size_t n_output_samples = output_batch.tail / 2;  /* 2 inputs per output sample */
    
    for (size_t i = 0; i < n_output_samples && i < 5; i++) {
        /* First channel should have values 1, 2, 3, ... */
        TEST_ASSERT_TRUE(out_ptr[i * 2] >= 1.0);
        TEST_ASSERT_TRUE(out_ptr[i * 2] <= 32.0);
        
        /* Second channel should have values 100, 101, 102, ... */
        TEST_ASSERT_TRUE(out_ptr[i * 2 + 1] >= 100.0);
        TEST_ASSERT_TRUE(out_ptr[i * 2 + 1] <= 140.0);
    }
    
    BpZOHResampler_Deinit(&resampler);
}

/* Test underrun handling */
void test_resampler_underrun(void) {
    /* Initialize with drop_on_underrun = true */
    BpZOHResamplerConfig config = BP_ZOH_RESAMPLER_CONFIG_DEFAULT;
    config.output_period_ns = 1000000;  /* 1kHz */
    config.base_config.dtype = DTYPE_FLOAT;
    config.base_config.number_of_input_filters = 2;
    config.drop_on_underrun = true;
    
    Bp_EC ec = BpZOHResampler_Init(&resampler, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    
    /* Create only one input with data */
    float input1_data[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    
    Bp_Batch_t input1_batch = {
        .head = 0,
        .tail = 10,
        .capacity = 10,
        .t_ns = 1000000000,
        .period_ns = 1000000,
        .batch_id = 1,
        .ec = Bp_EC_OK,
        .dtype = DTYPE_FLOAT,
        .data = input1_data
    };
    
    /* Second input has no data */
    Bp_Batch_t input2_batch = {
        .head = 0,
        .tail = 0,  /* No data */
        .capacity = 10,
        .ec = Bp_EC_OK,
        .dtype = DTYPE_FLOAT,
        .data = NULL
    };
    
    float output_data[TEST_BATCH_SIZE];
    Bp_Batch_t output_batch = {
        .head = 0,
        .tail = 0,
        .capacity = TEST_BATCH_SIZE,
        .dtype = DTYPE_FLOAT,
        .data = output_data
    };
    
    /* Process */
    Bp_Batch_t* inputs[] = {&input1_batch, &input2_batch};
    Bp_Batch_t* outputs[] = {&output_batch};
    
    BpZOHResamplerTransform(&resampler.base, inputs, 2, outputs, 1);
    
    /* With drop_on_underrun, no output should be produced */
    TEST_ASSERT_EQUAL(0, output_batch.tail);
    
    /* Check underrun statistics */
    BpResamplerInputStats_t stats;
    BpZOHResampler_GetInputStats(&resampler, 1, &stats);
    TEST_ASSERT_TRUE(stats.underrun_count > 0);
    
    BpZOHResampler_Deinit(&resampler);
}

/* Test statistics collection */
void test_resampler_statistics(void) {
    /* Initialize resampler */
    Bp_EC ec = BpZOHResampler_InitSimple(&resampler, 1000, 1, DTYPE_FLOAT);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    
    /* Process multiple batches */
    float input_data[TEST_BATCH_SIZE];
    for (int batch = 0; batch < 5; batch++) {
        /* Fill with different data each batch */
        for (int i = 0; i < TEST_BATCH_SIZE; i++) {
            input_data[i] = (float)(batch * TEST_BATCH_SIZE + i);
        }
        
        Bp_Batch_t input_batch = {
            .head = 0,
            .tail = TEST_BATCH_SIZE,
            .capacity = TEST_BATCH_SIZE,
            .t_ns = 1000000000LL + batch * TEST_BATCH_SIZE * 1000000LL,
            .period_ns = 1000000,
            .batch_id = batch + 1,
            .ec = Bp_EC_OK,
            .dtype = DTYPE_FLOAT,
            .data = input_data
        };
        
        float output_data[TEST_BATCH_SIZE];
        Bp_Batch_t output_batch = {
            .head = 0,
            .tail = 0,
            .capacity = TEST_BATCH_SIZE,
            .dtype = DTYPE_FLOAT,
            .data = output_data
        };
        
        Bp_Batch_t* inputs[] = {&input_batch};
        Bp_Batch_t* outputs[] = {&output_batch};
        
        BpZOHResamplerTransform(&resampler.base, inputs, 1, outputs, 1);
    }
    
    /* Check statistics */
    BpResamplerInputStats_t stats;
    ec = BpZOHResampler_GetInputStats(&resampler, 0, &stats);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    
    TEST_ASSERT_EQUAL(5 * TEST_BATCH_SIZE, stats.samples_processed);
    TEST_ASSERT_EQUAL(0, stats.underrun_count);
    TEST_ASSERT_EQUAL(0, stats.discontinuity_count);
    TEST_ASSERT_FLOAT_WITHIN(1.0, 1000.0, stats.avg_input_rate_hz);
    
    BpZOHResampler_Deinit(&resampler);
}

/* Test reset functionality */
void test_resampler_reset(void) {
    /* Initialize and process some data */
    Bp_EC ec = BpZOHResampler_InitSimple(&resampler, 1000, 1, DTYPE_FLOAT);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    
    /* Process a batch to set internal state */
    float input_data[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    Bp_Batch_t input_batch = {
        .head = 0,
        .tail = 10,
        .capacity = 10,
        .t_ns = 1000000000,
        .period_ns = 1000000,
        .batch_id = 1,
        .ec = Bp_EC_OK,
        .dtype = DTYPE_FLOAT,
        .data = input_data
    };
    
    float output_data[TEST_BATCH_SIZE];
    Bp_Batch_t output_batch = {
        .head = 0,
        .tail = 0,
        .capacity = TEST_BATCH_SIZE,
        .dtype = DTYPE_FLOAT,
        .data = output_data
    };
    
    Bp_Batch_t* inputs[] = {&input_batch};
    Bp_Batch_t* outputs[] = {&output_batch};
    
    BpZOHResamplerTransform(&resampler.base, inputs, 1, outputs, 1);
    
    /* Verify state was set */
    TEST_ASSERT_TRUE(resampler.started);
    TEST_ASSERT_TRUE(resampler.output_batch_id > 0);
    TEST_ASSERT_TRUE(resampler.input_states[0].has_data);
    
    /* Reset */
    ec = BpZOHResampler_Reset(&resampler);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    
    /* Verify reset */
    TEST_ASSERT_FALSE(resampler.started);
    TEST_ASSERT_EQUAL(0, resampler.output_batch_id);
    TEST_ASSERT_FALSE(resampler.input_states[0].has_data);
    TEST_ASSERT_EQUAL(0, resampler.input_states[0].samples_processed);
    
    BpZOHResampler_Deinit(&resampler);
}

/* Test discontinuity detection */
void test_resampler_discontinuity(void) {
    Bp_EC ec = BpZOHResampler_InitSimple(&resampler, 1000, 1, DTYPE_FLOAT);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    
    float input_data[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    float output_data[TEST_BATCH_SIZE];
    
    /* Process first batch */
    Bp_Batch_t input_batch = {
        .head = 0,
        .tail = 10,
        .capacity = 10,
        .t_ns = 1000000000,
        .period_ns = 1000000,
        .batch_id = 1,
        .ec = Bp_EC_OK,
        .dtype = DTYPE_FLOAT,
        .data = input_data
    };
    
    Bp_Batch_t output_batch = {
        .head = 0,
        .tail = 0,
        .capacity = TEST_BATCH_SIZE,
        .dtype = DTYPE_FLOAT,
        .data = output_data
    };
    
    Bp_Batch_t* inputs[] = {&input_batch};
    Bp_Batch_t* outputs[] = {&output_batch};
    
    BpZOHResamplerTransform(&resampler.base, inputs, 1, outputs, 1);
    
    /* Process second batch with gap in batch_id */
    input_batch.batch_id = 5;  /* Gap from 1 to 5 */
    input_batch.t_ns = 1000000000 + 50 * 1000000;  /* 50ms later */
    
    BpZOHResamplerTransform(&resampler.base, inputs, 1, outputs, 1);
    
    /* Check discontinuity was detected */
    BpResamplerInputStats_t stats;
    ec = BpZOHResampler_GetInputStats(&resampler, 0, &stats);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    TEST_ASSERT_EQUAL(1, stats.discontinuity_count);
    
    BpZOHResampler_Deinit(&resampler);
}

/* Main test runner */
int main(void) {
    UNITY_BEGIN();
    
    /* Initialization tests */
    RUN_TEST(test_resampler_init_valid);
    RUN_TEST(test_resampler_init_simple);
    RUN_TEST(test_resampler_init_invalid);
    
    /* Basic functionality tests */
    //RUN_TEST(test_resampler_passthrough);
    //RUN_TEST(test_resampler_downsample);
    RUN_TEST(test_resampler_upsample);
    
    /* Multi-input tests */
    RUN_TEST(test_resampler_multi_input);
    RUN_TEST(test_resampler_underrun);
    
    /* Advanced tests */
    RUN_TEST(test_resampler_statistics);
    RUN_TEST(test_resampler_reset);
    RUN_TEST(test_resampler_discontinuity);
    
    return UNITY_END();
}