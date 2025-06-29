#include <math.h>
#include "math_ops.h"
#include "unity.h"

// Test fixtures
static BpMultiplyConst_t multiply_const;
static BpMultiplyMulti_t multiply_multi;

void setUp(void)
{
    // Reset all structures
    memset(&multiply_const, 0, sizeof(multiply_const));
    memset(&multiply_multi, 0, sizeof(multiply_multi));
}

void tearDown(void)
{
    // Cleanup if needed
    Bp_Filter_Stop(&multiply_const.base);
    Bp_Filter_Stop(&multiply_multi.base);
    BpFilter_Deinit(&multiply_const.base);
    BpFilter_Deinit(&multiply_multi.base);
}

// Helper to create test batch
static Bp_Batch_t create_test_batch(void* data, size_t head, size_t tail,
                                    SampleDtype_t dtype)
{
    Bp_Batch_t batch = {.head = head,
                        .tail = tail,
                        .capacity = 64,
                        .t_ns = 1000000,
                        .period_ns = 1000,
                        .batch_id = 1,
                        .ec = Bp_EC_OK,
                        .meta = NULL,
                        .dtype = dtype,
                        .data = data};
    return batch;
}

// Test BpMultiplyConst initialization
void test_multiply_const_init_valid(void)
{
    BpMultiplyConstConfig config = {.math_config = BP_MATH_OP_CONFIG_DEFAULT,
                                    .value = 2.5f};
    config.math_config.base_config.dtype = DTYPE_FLOAT;

    Bp_EC ec = BpMultiplyConst_Init(&multiply_const, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, multiply_const.scale);
}

void test_multiply_const_init_null(void)
{
    BpMultiplyConstConfig config = {.math_config = BP_MATH_OP_CONFIG_DEFAULT,
                                    .value = 2.5f};

    Bp_EC ec = BpMultiplyConst_Init(NULL, &config);
    TEST_ASSERT_EQUAL(Bp_EC_NULL_FILTER, ec);

    ec = BpMultiplyConst_Init(&multiply_const, NULL);
    TEST_ASSERT_EQUAL(Bp_EC_NULL_FILTER, ec);
}

void test_multiply_const_init_invalid_dtype(void)
{
    BpMultiplyConstConfig config = {.math_config = BP_MATH_OP_CONFIG_DEFAULT,
                                    .value = 2.5f};
    config.math_config.base_config.dtype = DTYPE_NDEF;

    Bp_EC ec = BpMultiplyConst_Init(&multiply_const, &config);
    TEST_ASSERT_EQUAL(Bp_EC_INVALID_DTYPE, ec);
}

// Test BpMultiplyConst transform
void test_multiply_const_transform_float(void)
{
    // Initialize
    BpMultiplyConstConfig config = {.math_config = BP_MATH_OP_CONFIG_DEFAULT,
                                    .value = 2.0f};
    config.math_config.base_config.dtype = DTYPE_FLOAT;

    Bp_EC ec = BpMultiplyConst_Init(&multiply_const, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Create test data
    float input_data[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    float output_data[10] = {0};

    Bp_Batch_t input_batch = create_test_batch(input_data, 0, 10, DTYPE_FLOAT);
    Bp_Batch_t output_batch =
        create_test_batch(output_data, 0, 10, DTYPE_FLOAT);

    Bp_Batch_t* inputs[] = {&input_batch};
    Bp_Batch_t* outputs[] = {&output_batch};

    // Transform
    BpMultiplyConstTransform(&multiply_const.base, inputs, 1, outputs, 1);

    // Verify
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_FLOAT(input_data[i] * 2.0f, output_data[i]);
    }

    // Verify metadata was copied correctly
    TEST_ASSERT_EQUAL(input_batch.head, output_batch.head);
    TEST_ASSERT_EQUAL(input_batch.tail, output_batch.tail);
    TEST_ASSERT_EQUAL(input_batch.t_ns, output_batch.t_ns);
    TEST_ASSERT_EQUAL(input_batch.period_ns, output_batch.period_ns);
    TEST_ASSERT_EQUAL(input_batch.batch_id, output_batch.batch_id);
    TEST_ASSERT_EQUAL(input_batch.ec, output_batch.ec);
    TEST_ASSERT_EQUAL(input_batch.dtype, output_batch.dtype);
}

void test_multiply_const_transform_int(void)
{
    // Initialize
    BpMultiplyConstConfig config = {.math_config = BP_MATH_OP_CONFIG_DEFAULT,
                                    .value = 3.0f};
    config.math_config.base_config.dtype = DTYPE_INT;

    Bp_EC ec = BpMultiplyConst_Init(&multiply_const, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Create test data
    int input_data[5] = {1, 2, 3, 4, 5};
    int output_data[5] = {0};

    Bp_Batch_t input_batch = create_test_batch(input_data, 0, 5, DTYPE_INT);
    Bp_Batch_t output_batch = create_test_batch(output_data, 0, 5, DTYPE_INT);

    Bp_Batch_t* inputs[] = {&input_batch};
    Bp_Batch_t* outputs[] = {&output_batch};

    // Transform
    BpMultiplyConstTransform(&multiply_const.base, inputs, 1, outputs, 1);

    // Verify
    int expected[5] = {3, 6, 9, 12, 15};
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_INT(expected[i], output_data[i]);
    }
}

void test_multiply_const_transform_unsigned(void)
{
    // Initialize
    BpMultiplyConstConfig config = {.math_config = BP_MATH_OP_CONFIG_DEFAULT,
                                    .value = 2.0f};
    config.math_config.base_config.dtype = DTYPE_UNSIGNED;

    Bp_EC ec = BpMultiplyConst_Init(&multiply_const, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Create test data
    unsigned input_data[5] = {1, 2, 3, 4, 5};
    unsigned output_data[5] = {0};

    Bp_Batch_t input_batch =
        create_test_batch(input_data, 0, 5, DTYPE_UNSIGNED);
    Bp_Batch_t output_batch =
        create_test_batch(output_data, 0, 5, DTYPE_UNSIGNED);

    Bp_Batch_t* inputs[] = {&input_batch};
    Bp_Batch_t* outputs[] = {&output_batch};

    // Transform
    BpMultiplyConstTransform(&multiply_const.base, inputs, 1, outputs, 1);

    // Verify
    unsigned expected[5] = {2, 4, 6, 8, 10};
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_UINT(expected[i], output_data[i]);
    }
}

void test_multiply_const_transform_empty_batch(void)
{
    // Initialize
    BpMultiplyConstConfig config = {.math_config = BP_MATH_OP_CONFIG_DEFAULT,
                                    .value = 2.0f};
    config.math_config.base_config.dtype = DTYPE_FLOAT;

    Bp_EC ec = BpMultiplyConst_Init(&multiply_const, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Create empty batch (head == tail)
    float input_data[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    float output_data[10] = {0};

    Bp_Batch_t input_batch =
        create_test_batch(input_data, 5, 5, DTYPE_FLOAT);  // head == tail
    Bp_Batch_t output_batch =
        create_test_batch(output_data, 0, 10, DTYPE_FLOAT);

    Bp_Batch_t* inputs[] = {&input_batch};
    Bp_Batch_t* outputs[] = {&output_batch};

    // Transform should do nothing
    BpMultiplyConstTransform(&multiply_const.base, inputs, 1, outputs, 1);

    // Verify metadata was still copied
    TEST_ASSERT_EQUAL(input_batch.head, output_batch.head);
    TEST_ASSERT_EQUAL(input_batch.tail, output_batch.tail);

    // Output data should remain unchanged (zeros)
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_FLOAT(0.0f, output_data[i]);
    }
}

// Test BpMultiplyMulti initialization
void test_multiply_multi_init_valid(void)
{
    BpMultiplyMultiConfig config = {.math_config = BP_MATH_OP_CONFIG_DEFAULT};
    config.math_config.base_config.dtype = DTYPE_FLOAT;
    config.math_config.base_config.number_of_input_filters = 3;

    Bp_EC ec = BpMultiplyMulti_Init(&multiply_multi, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
}

void test_multiply_multi_init_null(void)
{
    BpMultiplyMultiConfig config = {.math_config = BP_MATH_OP_CONFIG_DEFAULT};

    Bp_EC ec = BpMultiplyMulti_Init(NULL, &config);
    TEST_ASSERT_EQUAL(Bp_EC_NULL_FILTER, ec);

    ec = BpMultiplyMulti_Init(&multiply_multi, NULL);
    TEST_ASSERT_EQUAL(Bp_EC_NULL_FILTER, ec);
}

void test_multiply_multi_init_invalid_inputs(void)
{
    BpMultiplyMultiConfig config = {.math_config = BP_MATH_OP_CONFIG_DEFAULT};
    config.math_config.base_config.dtype = DTYPE_FLOAT;

    // Too few inputs
    config.math_config.base_config.number_of_input_filters = 1;
    Bp_EC ec = BpMultiplyMulti_Init(&multiply_multi, &config);
    TEST_ASSERT_EQUAL(Bp_EC_INVALID_CONFIG, ec);

    // Too many inputs
    config.math_config.base_config.number_of_input_filters = MAX_SOURCES + 1;
    ec = BpMultiplyMulti_Init(&multiply_multi, &config);
    TEST_ASSERT_EQUAL(Bp_EC_INVALID_CONFIG, ec);
}

// Test BpMultiplyMulti transform
void test_multiply_multi_transform_float(void)
{
    // Initialize
    BpMultiplyMultiConfig config = {.math_config = BP_MATH_OP_CONFIG_DEFAULT};
    config.math_config.base_config.dtype = DTYPE_FLOAT;
    config.math_config.base_config.number_of_input_filters = 3;

    Bp_EC ec = BpMultiplyMulti_Init(&multiply_multi, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Create test data
    float input1[5] = {1, 2, 3, 4, 5};
    float input2[5] = {2, 2, 2, 2, 2};
    float input3[5] = {3, 3, 3, 3, 3};
    float output[5] = {0};

    Bp_Batch_t batch1 = create_test_batch(input1, 0, 5, DTYPE_FLOAT);
    Bp_Batch_t batch2 = create_test_batch(input2, 0, 5, DTYPE_FLOAT);
    Bp_Batch_t batch3 = create_test_batch(input3, 0, 5, DTYPE_FLOAT);
    Bp_Batch_t output_batch = create_test_batch(output, 0, 5, DTYPE_FLOAT);

    Bp_Batch_t* inputs[] = {&batch1, &batch2, &batch3};
    Bp_Batch_t* outputs[] = {&output_batch};

    // Transform
    BpMultiplyMultiTransform(&multiply_multi.base, inputs, 3, outputs, 1);

    // Verify: output[i] = input1[i] * input2[i] * input3[i]
    float expected[5] = {6, 12, 18, 24,
                         30};  // 1*2*3, 2*2*3, 3*2*3, 4*2*3, 5*2*3
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_FLOAT(expected[i], output[i]);
    }

    // Verify no error occurred
    TEST_ASSERT_EQUAL(Bp_EC_OK, multiply_multi.base.worker_err_info.ec);
}

void test_multiply_multi_transform_int(void)
{
    // Initialize
    BpMultiplyMultiConfig config = {.math_config = BP_MATH_OP_CONFIG_DEFAULT};
    config.math_config.base_config.dtype = DTYPE_INT;
    config.math_config.base_config.number_of_input_filters = 2;

    Bp_EC ec = BpMultiplyMulti_Init(&multiply_multi, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Create test data
    int input1[4] = {1, 2, 3, 4};
    int input2[4] = {5, 6, 7, 8};
    int output[4] = {0};

    Bp_Batch_t batch1 = create_test_batch(input1, 0, 4, DTYPE_INT);
    Bp_Batch_t batch2 = create_test_batch(input2, 0, 4, DTYPE_INT);
    Bp_Batch_t output_batch = create_test_batch(output, 0, 4, DTYPE_INT);

    Bp_Batch_t* inputs[] = {&batch1, &batch2};
    Bp_Batch_t* outputs[] = {&output_batch};

    // Transform
    BpMultiplyMultiTransform(&multiply_multi.base, inputs, 2, outputs, 1);

    // Verify: output[i] = input1[i] * input2[i]
    int expected[4] = {5, 12, 21, 32};  // 1*5, 2*6, 3*7, 4*8
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT(expected[i], output[i]);
    }
}

void test_multiply_multi_insufficient_inputs(void)
{
    // Initialize
    BpMultiplyMultiConfig config = {.math_config = BP_MATH_OP_CONFIG_DEFAULT};
    config.math_config.base_config.dtype = DTYPE_FLOAT;
    config.math_config.base_config.number_of_input_filters = 2;

    Bp_EC ec = BpMultiplyMulti_Init(&multiply_multi, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Create test data with only 1 input (insufficient)
    float input1[5] = {1, 2, 3, 4, 5};
    float output[5] = {0};

    Bp_Batch_t batch1 = create_test_batch(input1, 0, 5, DTYPE_FLOAT);
    Bp_Batch_t output_batch = create_test_batch(output, 0, 5, DTYPE_FLOAT);

    Bp_Batch_t* inputs[] = {&batch1};
    Bp_Batch_t* outputs[] = {&output_batch};

    // Transform should set error
    BpMultiplyMultiTransform(&multiply_multi.base, inputs, 1, outputs, 1);

    TEST_ASSERT_EQUAL(Bp_EC_NOINPUT, multiply_multi.base.worker_err_info.ec);
}

void test_multiply_multi_dtype_mismatch(void)
{
    // Initialize
    BpMultiplyMultiConfig config = {.math_config = BP_MATH_OP_CONFIG_DEFAULT};
    config.math_config.base_config.dtype = DTYPE_FLOAT;
    config.math_config.base_config.number_of_input_filters = 2;

    Bp_EC ec = BpMultiplyMulti_Init(&multiply_multi, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Create mismatched data
    float input1[5] = {1, 2, 3, 4, 5};
    int input2[5] = {2, 2, 2, 2, 2};
    float output[5] = {0};

    Bp_Batch_t batch1 = create_test_batch(input1, 0, 5, DTYPE_FLOAT);
    Bp_Batch_t batch2 =
        create_test_batch(input2, 0, 5, DTYPE_INT);  // Wrong type!
    Bp_Batch_t output_batch = create_test_batch(output, 0, 5, DTYPE_FLOAT);

    Bp_Batch_t* inputs[] = {&batch1, &batch2};
    Bp_Batch_t* outputs[] = {&output_batch};

    // Transform should set error
    BpMultiplyMultiTransform(&multiply_multi.base, inputs, 2, outputs, 1);

    TEST_ASSERT_EQUAL(Bp_EC_DTYPE_MISMATCH,
                      multiply_multi.base.worker_err_info.ec);
}

void test_multiply_multi_size_mismatch(void)
{
    // Initialize
    BpMultiplyMultiConfig config = {.math_config = BP_MATH_OP_CONFIG_DEFAULT};
    config.math_config.base_config.dtype = DTYPE_FLOAT;
    config.math_config.base_config.number_of_input_filters = 2;

    Bp_EC ec = BpMultiplyMulti_Init(&multiply_multi, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Create different sized data
    float input1[5] = {1, 2, 3, 4, 5};
    float input2[3] = {2, 2, 2};
    float output[5] = {0};

    Bp_Batch_t batch1 = create_test_batch(input1, 0, 5, DTYPE_FLOAT);
    Bp_Batch_t batch2 =
        create_test_batch(input2, 0, 3, DTYPE_FLOAT);  // Different size!
    Bp_Batch_t output_batch = create_test_batch(output, 0, 5, DTYPE_FLOAT);

    Bp_Batch_t* inputs[] = {&batch1, &batch2};
    Bp_Batch_t* outputs[] = {&output_batch};

    // Transform should set error
    BpMultiplyMultiTransform(&multiply_multi.base, inputs, 2, outputs, 1);

    TEST_ASSERT_EQUAL(Bp_EC_WIDTH_MISMATCH,
                      multiply_multi.base.worker_err_info.ec);
}

// Main test runner
int main(void)
{
    UNITY_BEGIN();

    // BpMultiplyConst tests
    RUN_TEST(test_multiply_const_init_valid);
    RUN_TEST(test_multiply_const_init_null);
    RUN_TEST(test_multiply_const_init_invalid_dtype);
    RUN_TEST(test_multiply_const_transform_float);
    RUN_TEST(test_multiply_const_transform_int);
    RUN_TEST(test_multiply_const_transform_unsigned);
    RUN_TEST(test_multiply_const_transform_empty_batch);

    // BpMultiplyMulti tests
    RUN_TEST(test_multiply_multi_init_valid);
    RUN_TEST(test_multiply_multi_init_null);
    RUN_TEST(test_multiply_multi_init_invalid_inputs);
    RUN_TEST(test_multiply_multi_transform_float);
    RUN_TEST(test_multiply_multi_transform_int);
    RUN_TEST(test_multiply_multi_insufficient_inputs);
    RUN_TEST(test_multiply_multi_dtype_mismatch);
    RUN_TEST(test_multiply_multi_size_mismatch);

    return UNITY_END();
}