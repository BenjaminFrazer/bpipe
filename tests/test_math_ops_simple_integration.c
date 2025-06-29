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

// Test complete data flow without threading
void test_simple_pipeline_multiply_const(void)
{
    BpMultiplyConst_t multiply;

    // Initialize multiply
    BpMultiplyConstConfig config = {.math_config = BP_MATH_OP_CONFIG_DEFAULT,
                                    .value = 3.0f};
    config.math_config.base_config.dtype = DTYPE_FLOAT;

    Bp_EC ec = BpMultiplyConst_Init(&multiply, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, multiply.scale);

    // Create test data: signal_gen → multiply_const
    float signal_data[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    float output_data[8] = {0};

    Bp_Batch_t input_batch = create_test_batch(signal_data, 0, 8, DTYPE_FLOAT);
    Bp_Batch_t output_batch = create_test_batch(output_data, 0, 8, DTYPE_FLOAT);

    Bp_Batch_t* inputs[] = {&input_batch};
    Bp_Batch_t* outputs[] = {&output_batch};

    // Apply transform manually
    BpMultiplyConstTransform(&multiply.base, inputs, 1, outputs, 1);

    // Verify results: output = input * 3.0
    float expected[8] = {3.0f, 6.0f, 9.0f, 12.0f, 15.0f, 18.0f, 21.0f, 24.0f};
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_FLOAT(expected[i], output_data[i]);
    }

    // Verify metadata was copied correctly
    TEST_ASSERT_EQUAL(input_batch.head, output_batch.head);
    TEST_ASSERT_EQUAL(input_batch.tail, output_batch.tail);
    TEST_ASSERT_EQUAL(input_batch.t_ns, output_batch.t_ns);
    TEST_ASSERT_EQUAL(input_batch.period_ns, output_batch.period_ns);
    TEST_ASSERT_EQUAL(input_batch.batch_id, output_batch.batch_id);
    TEST_ASSERT_EQUAL(input_batch.ec, output_batch.ec);
    TEST_ASSERT_EQUAL(input_batch.dtype, output_batch.dtype);

    // Cleanup
    BpFilter_Deinit(&multiply.base);
}

// Test multi-input data flow without threading
void test_simple_pipeline_multiply_multi(void)
{
    BpMultiplyMulti_t multiply;

    // Initialize multiply (3 inputs)
    BpMultiplyMultiConfig config = {.math_config = BP_MATH_OP_CONFIG_DEFAULT};
    config.math_config.base_config.dtype = DTYPE_FLOAT;
    config.math_config.base_config.number_of_input_filters = 3;

    Bp_EC ec = BpMultiplyMulti_Init(&multiply, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Create test data: three signals → multiply_multi
    float signal1[5] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    float signal2[5] = {2.0f, 2.0f, 2.0f, 2.0f, 2.0f};
    float signal3[5] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    float output_data[5] = {0};

    Bp_Batch_t batch1 = create_test_batch(signal1, 0, 5, DTYPE_FLOAT);
    Bp_Batch_t batch2 = create_test_batch(signal2, 0, 5, DTYPE_FLOAT);
    Bp_Batch_t batch3 = create_test_batch(signal3, 0, 5, DTYPE_FLOAT);
    Bp_Batch_t output_batch = create_test_batch(output_data, 0, 5, DTYPE_FLOAT);

    Bp_Batch_t* inputs[] = {&batch1, &batch2, &batch3};
    Bp_Batch_t* outputs[] = {&output_batch};

    // Apply transform manually
    BpMultiplyMultiTransform(&multiply.base, inputs, 3, outputs, 1);

    // Verify results: output = input1 * input2 * input3
    float expected[5] = {1.0f, 2.0f, 3.0f, 4.0f,
                         5.0f};  // * 2.0 * 0.5 = original values
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_FLOAT(expected[i], output_data[i]);
    }

    // Verify no error occurred
    TEST_ASSERT_EQUAL(Bp_EC_OK, multiply.base.worker_err_info.ec);

    // Cleanup
    BpFilter_Deinit(&multiply.base);
}

// Test chained operations without threading
void test_simple_chained_operations(void)
{
    BpMultiplyConst_t multiply1, multiply2;

    // Initialize first multiply (scale by 2)
    BpMultiplyConstConfig config1 = {.math_config = BP_MATH_OP_CONFIG_DEFAULT,
                                     .value = 2.0f};
    config1.math_config.base_config.dtype = DTYPE_FLOAT;

    Bp_EC ec = BpMultiplyConst_Init(&multiply1, &config1);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Initialize second multiply (scale by 1.5)
    BpMultiplyConstConfig config2 = {.math_config = BP_MATH_OP_CONFIG_DEFAULT,
                                     .value = 1.5f};
    config2.math_config.base_config.dtype = DTYPE_FLOAT;

    ec = BpMultiplyConst_Init(&multiply2, &config2);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Create test data: signal → multiply1 → multiply2
    float signal_data[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    float intermediate_data[4] = {0};
    float final_data[4] = {0};

    Bp_Batch_t input_batch = create_test_batch(signal_data, 0, 4, DTYPE_FLOAT);
    Bp_Batch_t intermediate_batch =
        create_test_batch(intermediate_data, 0, 4, DTYPE_FLOAT);
    Bp_Batch_t final_batch = create_test_batch(final_data, 0, 4, DTYPE_FLOAT);

    // Stage 1: signal → multiply1
    Bp_Batch_t* inputs1[] = {&input_batch};
    Bp_Batch_t* outputs1[] = {&intermediate_batch};

    BpMultiplyConstTransform(&multiply1.base, inputs1, 1, outputs1, 1);

    // Stage 2: multiply1 → multiply2
    Bp_Batch_t* inputs2[] = {&intermediate_batch};
    Bp_Batch_t* outputs2[] = {&final_batch};

    BpMultiplyConstTransform(&multiply2.base, inputs2, 1, outputs2, 1);

    // Verify results: output = input * 2.0 * 1.5 = input * 3.0
    float expected[4] = {3.0f, 6.0f, 9.0f, 12.0f};
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_FLOAT(expected[i], final_data[i]);
    }

    // Verify intermediate results as well
    float expected_intermediate[4] = {2.0f, 4.0f, 6.0f, 8.0f};
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_FLOAT(expected_intermediate[i], intermediate_data[i]);
    }

    // Cleanup
    BpFilter_Deinit(&multiply1.base);
    BpFilter_Deinit(&multiply2.base);
}

// Test mixed data types
void test_simple_mixed_dtypes(void)
{
    BpMultiplyConst_t int_multiply;
    BpMultiplyConst_t unsigned_multiply;

    // Integer multiply
    BpMultiplyConstConfig int_config = {
        .math_config = BP_MATH_OP_CONFIG_DEFAULT, .value = 3.0f};
    int_config.math_config.base_config.dtype = DTYPE_INT;

    Bp_EC ec = BpMultiplyConst_Init(&int_multiply, &int_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Unsigned multiply
    BpMultiplyConstConfig unsigned_config = {
        .math_config = BP_MATH_OP_CONFIG_DEFAULT, .value = 2.0f};
    unsigned_config.math_config.base_config.dtype = DTYPE_UNSIGNED;

    ec = BpMultiplyConst_Init(&unsigned_multiply, &unsigned_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);

    // Test integer path
    int int_data[3] = {5, 10, 15};
    int int_output[3] = {0};

    Bp_Batch_t int_input = create_test_batch(int_data, 0, 3, DTYPE_INT);
    Bp_Batch_t int_out = create_test_batch(int_output, 0, 3, DTYPE_INT);

    Bp_Batch_t* int_inputs[] = {&int_input};
    Bp_Batch_t* int_outputs[] = {&int_out};

    BpMultiplyConstTransform(&int_multiply.base, int_inputs, 1, int_outputs, 1);

    int expected_int[3] = {15, 30, 45};
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_INT(expected_int[i], int_output[i]);
    }

    // Test unsigned path
    unsigned unsigned_data[3] = {10, 20, 30};
    unsigned unsigned_output[3] = {0};

    Bp_Batch_t unsigned_input =
        create_test_batch(unsigned_data, 0, 3, DTYPE_UNSIGNED);
    Bp_Batch_t unsigned_out =
        create_test_batch(unsigned_output, 0, 3, DTYPE_UNSIGNED);

    Bp_Batch_t* unsigned_inputs[] = {&unsigned_input};
    Bp_Batch_t* unsigned_outputs[] = {&unsigned_out};

    BpMultiplyConstTransform(&unsigned_multiply.base, unsigned_inputs, 1,
                             unsigned_outputs, 1);

    unsigned expected_unsigned[3] = {20, 40, 60};
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_UINT(expected_unsigned[i], unsigned_output[i]);
    }

    // Cleanup
    BpFilter_Deinit(&int_multiply.base);
    BpFilter_Deinit(&unsigned_multiply.base);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_simple_pipeline_multiply_const);
    RUN_TEST(test_simple_pipeline_multiply_multi);
    RUN_TEST(test_simple_chained_operations);
    RUN_TEST(test_simple_mixed_dtypes);

    return UNITY_END();
}