#define _DEFAULT_SOURCE
#include <unistd.h>
#include "math_ops.h"
#include "signal_gen.h"
#include "unity.h"

void setUp(void)
{
    // Unity setup function
}

void tearDown(void)
{
    // Unity teardown function
}

// Test complete pipeline with math operations
void test_pipeline_multiply_const(void)
{
    // Create signal generator → multiply → sink pipeline
    Bp_SignalGen_t source;
    BpMultiplyConst_t multiply;
    Bp_Filter_t sink;

    // Initialize source
    Bp_EC ec = BpSignalGen_Init(&source, BP_WAVE_SINE, 0.1f, 1.0f, 0.0f, 0.0f,
                                128, 64, 6);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Initialize multiply
    BpMultiplyConstConfig config = {.math_config = BP_MATH_OP_CONFIG_DEFAULT,
                                    .value = 2.0f};
    config.math_config.base_config.dtype = DTYPE_FLOAT;
    config.math_config.base_config.buffer_size = 128;
    config.math_config.base_config.batch_size = 64;
    config.math_config.base_config.number_of_batches_exponent = 6;
    config.math_config.base_config.number_of_input_filters = 1;
    config.math_config.base_config.auto_allocate_buffers = true;

    ec = BpMultiplyConst_Init(&multiply, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Initialize sink
    BpFilterConfig sink_config = BP_FILTER_CONFIG_DEFAULT;
    sink_config.dtype = DTYPE_FLOAT;
    sink_config.buffer_size = 128;
    sink_config.batch_size = 64;
    sink_config.number_of_batches_exponent = 6;
    sink_config.number_of_input_filters = 1;
    sink_config.auto_allocate_buffers = true;
    sink_config.transform = BpPassThroughTransform;

    ec = BpFilter_Init(&sink, &sink_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Connect pipeline: source → multiply → sink
    ec = Bp_add_sink(&source.base, &multiply.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    ec = Bp_add_sink(&multiply.base, &sink);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Start filters
    ec = Bp_Filter_Start(&source.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    ec = Bp_Filter_Start(&multiply.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    ec = Bp_Filter_Start(&sink);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Let it run briefly
    usleep(50000);  // 50ms

    // Stop filters
    Bp_Filter_Stop(&source.base);
    Bp_Filter_Stop(&multiply.base);
    Bp_Filter_Stop(&sink);

    // Verify filters ran without errors
    TEST_ASSERT_EQUAL(Bp_EC_OK, source.base.worker_err_info.ec);
    TEST_ASSERT_EQUAL(Bp_EC_OK, multiply.base.worker_err_info.ec);
    TEST_ASSERT_EQUAL(Bp_EC_OK, sink.worker_err_info.ec);

    // Cleanup
    BpFilter_Deinit(&source.base);
    BpFilter_Deinit(&multiply.base);
    BpFilter_Deinit(&sink);
}

void test_pipeline_multiply_multi(void)
{
    // Create two signal generators → multiply_multi → sink pipeline
    Bp_SignalGen_t source1, source2;
    BpMultiplyMulti_t multiply;
    Bp_Filter_t sink;

    // Initialize sources
    Bp_EC ec = BpSignalGen_Init(&source1, BP_WAVE_SINE, 0.1f, 1.0f, 0.0f, 0.0f,
                                128, 64, 6);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    ec = BpSignalGen_Init(&source2, BP_WAVE_SINE, 0.1f, 0.5f, 0.25f, 0.0f, 128,
                          64, 6);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Initialize multiply (2 inputs)
    BpMultiplyMultiConfig config = {.math_config = BP_MATH_OP_CONFIG_DEFAULT};
    config.math_config.base_config.dtype = DTYPE_FLOAT;
    config.math_config.base_config.buffer_size = 128;
    config.math_config.base_config.batch_size = 64;
    config.math_config.base_config.number_of_batches_exponent = 6;
    config.math_config.base_config.number_of_input_filters = 2;
    config.math_config.base_config.auto_allocate_buffers = true;

    ec = BpMultiplyMulti_Init(&multiply, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Initialize sink
    BpFilterConfig sink_config = BP_FILTER_CONFIG_DEFAULT;
    sink_config.dtype = DTYPE_FLOAT;
    sink_config.buffer_size = 128;
    sink_config.batch_size = 64;
    sink_config.number_of_batches_exponent = 6;
    sink_config.number_of_input_filters = 1;
    sink_config.auto_allocate_buffers = true;
    sink_config.transform = BpPassThroughTransform;

    ec = BpFilter_Init(&sink, &sink_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Connect pipeline: source1 → multiply ← source2 → sink
    ec = Bp_add_sink(&source1.base, &multiply.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    ec = Bp_add_sink(&source2.base, &multiply.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    ec = Bp_add_sink(&multiply.base, &sink);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Verify connections
    TEST_ASSERT_EQUAL(2, multiply.base.n_sources);
    TEST_ASSERT_EQUAL(1, multiply.base.n_sinks);

    // Start filters
    ec = Bp_Filter_Start(&source1.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    ec = Bp_Filter_Start(&source2.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    ec = Bp_Filter_Start(&multiply.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    ec = Bp_Filter_Start(&sink);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Let it run briefly
    usleep(50000);  // 50ms

    // Stop filters
    Bp_Filter_Stop(&source1.base);
    Bp_Filter_Stop(&source2.base);
    Bp_Filter_Stop(&multiply.base);
    Bp_Filter_Stop(&sink);

    // Verify filters ran without errors
    TEST_ASSERT_EQUAL(Bp_EC_OK, source1.base.worker_err_info.ec);
    TEST_ASSERT_EQUAL(Bp_EC_OK, source2.base.worker_err_info.ec);
    TEST_ASSERT_EQUAL(Bp_EC_OK, multiply.base.worker_err_info.ec);
    TEST_ASSERT_EQUAL(Bp_EC_OK, sink.worker_err_info.ec);

    // Cleanup
    BpFilter_Deinit(&source1.base);
    BpFilter_Deinit(&source2.base);
    BpFilter_Deinit(&multiply.base);
    BpFilter_Deinit(&sink);
}

void test_chained_math_operations(void)
{
    // Create pipeline: source → multiply_const → multiply_const → sink
    Bp_SignalGen_t source;
    BpMultiplyConst_t multiply1, multiply2;
    Bp_Filter_t sink;

    // Initialize source
    Bp_EC ec = BpSignalGen_Init(&source, BP_WAVE_SQUARE, 0.1f, 1.0f, 0.0f, 0.0f,
                                128, 64, 6);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Initialize first multiply (scale by 3)
    BpMultiplyConstConfig config1 = {.math_config = BP_MATH_OP_CONFIG_DEFAULT,
                                     .value = 3.0f};
    config1.math_config.base_config.dtype = DTYPE_FLOAT;
    config1.math_config.base_config.buffer_size = 128;
    config1.math_config.base_config.batch_size = 64;
    config1.math_config.base_config.number_of_batches_exponent = 6;

    ec = BpMultiplyConst_Init(&multiply1, &config1);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Initialize second multiply (scale by 2)
    BpMultiplyConstConfig config2 = {.math_config = BP_MATH_OP_CONFIG_DEFAULT,
                                     .value = 2.0f};
    config2.math_config.base_config.dtype = DTYPE_FLOAT;
    config2.math_config.base_config.buffer_size = 128;
    config2.math_config.base_config.batch_size = 64;
    config2.math_config.base_config.number_of_batches_exponent = 6;

    ec = BpMultiplyConst_Init(&multiply2, &config2);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Initialize sink
    BpFilterConfig sink_config = BP_FILTER_CONFIG_DEFAULT;
    sink_config.dtype = DTYPE_FLOAT;
    sink_config.buffer_size = 128;
    sink_config.batch_size = 64;
    sink_config.number_of_batches_exponent = 6;
    sink_config.number_of_input_filters = 1;
    sink_config.auto_allocate_buffers = true;
    sink_config.transform = BpPassThroughTransform;

    ec = BpFilter_Init(&sink, &sink_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Connect pipeline: source → multiply1 → multiply2 → sink
    ec = Bp_add_sink(&source.base, &multiply1.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    ec = Bp_add_sink(&multiply1.base, &multiply2.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    ec = Bp_add_sink(&multiply2.base, &sink);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Start filters
    ec = Bp_Filter_Start(&source.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    ec = Bp_Filter_Start(&multiply1.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    ec = Bp_Filter_Start(&multiply2.base);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    ec = Bp_Filter_Start(&sink);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Let it run briefly
    usleep(50000);  // 50ms

    // Stop filters
    Bp_Filter_Stop(&source.base);
    Bp_Filter_Stop(&multiply1.base);
    Bp_Filter_Stop(&multiply2.base);
    Bp_Filter_Stop(&sink);

    // Verify filters ran without errors
    TEST_ASSERT_EQUAL(Bp_EC_OK, source.base.worker_err_info.ec);
    TEST_ASSERT_EQUAL(Bp_EC_OK, multiply1.base.worker_err_info.ec);
    TEST_ASSERT_EQUAL(Bp_EC_OK, multiply2.base.worker_err_info.ec);
    TEST_ASSERT_EQUAL(Bp_EC_OK, sink.worker_err_info.ec);

    // Verify the scaling values are correct
    TEST_ASSERT_EQUAL_FLOAT(3.0f, multiply1.scale);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, multiply2.scale);

    // Cleanup
    BpFilter_Deinit(&source.base);
    BpFilter_Deinit(&multiply1.base);
    BpFilter_Deinit(&multiply2.base);
    BpFilter_Deinit(&sink);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_pipeline_multiply_const);
    RUN_TEST(test_pipeline_multiply_multi);
    RUN_TEST(test_chained_math_operations);

    return UNITY_END();
}