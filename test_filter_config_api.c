#include "bpipe/core.h"
#include <stdio.h>
#include <assert.h>

void test_basic_config_init() {
    printf("Testing basic configuration initialization...\n");
    
    Bp_Filter_t filter;
    BpFilterConfig config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_FLOAT,
        .buffer_size = 128,
        .batch_size = 64,
        .number_of_batches_exponent = 6,
        .number_of_input_filters = 1
    };
    
    Bp_EC result = BpFilter_Init(&filter, &config);
    assert(result == Bp_EC_OK);
    assert(filter.dtype == DTYPE_FLOAT);
    assert(filter.data_width == sizeof(float));
    assert(filter.transform == BpPassThroughTransform);
    assert(filter.overflow_behaviour == OVERFLOW_BLOCK);
    
    BpFilter_Deinit(&filter);
    printf("✓ Basic configuration initialization test passed\n");
}

void test_predefined_configs() {
    printf("Testing predefined configurations...\n");
    
    Bp_Filter_t filter;
    Bp_EC result = BpFilter_Init(&filter, &BP_CONFIG_FLOAT_STANDARD);
    assert(result == Bp_EC_OK);
    assert(filter.dtype == DTYPE_FLOAT);
    assert(filter.data_width == sizeof(float));
    
    BpFilter_Deinit(&filter);
    printf("✓ Predefined configuration test passed\n");
}

void test_config_validation() {
    printf("Testing configuration validation...\n");
    
    /* Test NULL config */
    Bp_EC result = BpFilterConfig_Validate(NULL);
    assert(result == Bp_EC_CONFIG_REQUIRED);
    
    /* Test missing transform */
    BpFilterConfig bad_config = {
        .transform = NULL,
        .dtype = DTYPE_FLOAT,
        .buffer_size = 128,
        .batch_size = 64,
        .number_of_batches_exponent = 6,
        .number_of_input_filters = 1
    };
    result = BpFilterConfig_Validate(&bad_config);
    assert(result == Bp_EC_CONFIG_REQUIRED);
    
    /* Test invalid dtype */
    bad_config.transform = BpPassThroughTransform;
    bad_config.dtype = DTYPE_MAX;
    result = BpFilterConfig_Validate(&bad_config);
    assert(result == Bp_EC_INVALID_DTYPE);
    
    /* Test valid config */
    BpFilterConfig good_config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_FLOAT,
        .buffer_size = 128,
        .batch_size = 64,
        .number_of_batches_exponent = 6,
        .number_of_input_filters = 1
    };
    result = BpFilterConfig_Validate(&good_config);
    assert(result == Bp_EC_OK);
    
    printf("✓ Configuration validation test passed\n");
}

void test_type_checking() {
    printf("Testing type checking on connections...\n");
    
    Bp_Filter_t source, sink;
    BpTypeError error;
    
    /* Initialize source as float */
    BpFilterConfig source_config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_FLOAT,
        .buffer_size = 128,
        .batch_size = 64,
        .number_of_batches_exponent = 6,
        .number_of_input_filters = 1
    };
    BpFilter_Init(&source, &source_config);
    
    /* Initialize sink as int - should cause type mismatch */
    BpFilterConfig sink_config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_INT,
        .buffer_size = 128,
        .batch_size = 64,
        .number_of_batches_exponent = 6,
        .number_of_input_filters = 1
    };
    BpFilter_Init(&sink, &sink_config);
    
    /* Test type mismatch detection */
    Bp_EC result = Bp_add_sink_with_error(&source, &sink, &error);
    assert(result == Bp_EC_DTYPE_MISMATCH);
    assert(error.code == Bp_EC_DTYPE_MISMATCH);
    assert(error.expected_type == DTYPE_INT);
    assert(error.actual_type == DTYPE_FLOAT);
    
    /* Test compatible types */
    sink.dtype = DTYPE_FLOAT;
    sink.data_width = sizeof(float);
    result = Bp_add_sink_with_error(&source, &sink, &error);
    assert(result == Bp_EC_OK);
    assert(error.code == Bp_EC_OK);
    
    BpFilter_Deinit(&source);
    BpFilter_Deinit(&sink);
    printf("✓ Type checking test passed\n");
}

void test_auto_allocation() {
    printf("Testing automatic buffer allocation...\n");
    
    Bp_Filter_t filter;
    BpFilterConfig config = {
        .transform = BpPassThroughTransform,
        .dtype = DTYPE_INT,
        .buffer_size = 64,
        .batch_size = 32,
        .number_of_batches_exponent = 5,
        .number_of_input_filters = 2,
        .auto_allocate_buffers = true
    };
    
    Bp_EC result = BpFilter_Init(&filter, &config);
    assert(result == Bp_EC_OK);
    assert(filter.dtype == DTYPE_INT);
    assert(filter.data_width == sizeof(int));
    
    /* Verify buffers were automatically allocated */
    assert(filter.input_buffers[0].data_ring != NULL);
    assert(filter.input_buffers[0].batch_ring != NULL);
    assert(filter.input_buffers[1].data_ring != NULL);
    assert(filter.input_buffers[1].batch_ring != NULL);
    
    BpFilter_Deinit(&filter);
    printf("✓ Automatic buffer allocation test passed\n");
}

int main() {
    printf("Running Filter Configuration API Tests\n");
    printf("=====================================\n");
    
    test_basic_config_init();
    test_predefined_configs();
    test_config_validation();
    test_type_checking();
    test_auto_allocation();
    
    printf("\n✓ All tests passed successfully!\n");
    return 0;
}