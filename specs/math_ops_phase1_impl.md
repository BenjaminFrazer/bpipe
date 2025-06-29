# Math Operations Phase 1 Implementation Plan

## Objective

Implement foundation math operations infrastructure with two representative operations:
- `BpMultiplyConst` - single input with constant multiplication
- `BpMultiplyMulti` - element-wise multiplication of multiple inputs

This phase establishes patterns for all future math operations while validating the initialization design.

## Deliverables

### 1. Core Header File (`bpipe/math_ops.h`)

```c
#ifndef BPIPE_MATH_OPS_H
#define BPIPE_MATH_OPS_H

#include "core.h"

/* Configuration structures */

// Base configuration for all math operations
typedef struct {
    BpFilterConfig base_config;     // Standard filter configuration
    bool in_place;                  // If true, reuse input buffer for output
    bool check_overflow;            // Enable overflow checking
    size_t simd_alignment;          // SIMD alignment hint (0 = auto)
} BpMathOpConfig;

// Single constant operation config
typedef struct {
    BpMathOpConfig math_config;
    float value;
} BpUnaryConstConfig;

// Multi-input operation config  
typedef struct {
    BpMathOpConfig math_config;
    // Note: n_inputs stored in base_config.number_of_input_filters
} BpMultiOpConfig;

/* Specific config typedefs */
typedef BpUnaryConstConfig BpMultiplyConstConfig;
typedef BpMultiOpConfig BpMultiplyMultiConfig;

/* Operation structures */

// Multiply by constant
typedef struct {
    Bp_Filter_t base;
    float scale;
} BpMultiplyConst_t;

// Multiply multiple inputs
typedef struct {
    Bp_Filter_t base;
    // n_inputs accessed via base.n_sources
} BpMultiplyMulti_t;

/* Initialization functions */
Bp_EC BpMultiplyConst_Init(BpMultiplyConst_t* op, const BpMultiplyConstConfig* config);
Bp_EC BpMultiplyMulti_Init(BpMultiplyMulti_t* op, const BpMultiplyMultiConfig* config);

/* Transform functions */
void BpMultiplyConstTransform(Bp_Filter_t* filter, Bp_Batch_t** inputs,
                             int n_inputs, Bp_Batch_t* const* outputs, int n_outputs);
void BpMultiplyMultiTransform(Bp_Filter_t* filter, Bp_Batch_t** inputs,
                             int n_inputs, Bp_Batch_t* const* outputs, int n_outputs);

/* Default configurations */
#define BP_MATH_OP_CONFIG_DEFAULT { \
    .base_config = BP_FILTER_CONFIG_DEFAULT, \
    .in_place = false, \
    .check_overflow = false, \
    .simd_alignment = 0 \
}

#endif /* BPIPE_MATH_OPS_H */
```

### 2. Core Implementation (`bpipe/math_ops.c`)

```c
#include "math_ops.h"
#include <math.h>

/* Internal helper for common initialization */
static Bp_EC BpMathOp_InitCommon(
    Bp_Filter_t* filter,
    const BpMathOpConfig* math_config,
    TransformFcn_t* transform
) {
    // Copy config and set transform
    BpFilterConfig config = math_config->base_config;
    config.transform = transform;
    
    // Validate dtype
    if (config.dtype == DTYPE_NDEF) {
        return Bp_EC_INVALID_DTYPE;
    }
    
    // Initialize base filter
    Bp_EC ec = BpFilter_Init(filter, &config);
    if (ec != Bp_EC_OK) {
        return ec;
    }
    
    // Store math-specific properties if needed
    // (for now, in_place and check_overflow are handled in transform)
    
    return Bp_EC_OK;
}

/* BpMultiplyConst Implementation */

Bp_EC BpMultiplyConst_Init(BpMultiplyConst_t* op, const BpMultiplyConstConfig* config) {
    if (!op || !config) {
        return Bp_EC_NULL_FILTER;
    }
    
    // Store operation-specific parameter
    op->scale = config->value;
    
    // Initialize base filter
    return BpMathOp_InitCommon(&op->base, &config->math_config, BpMultiplyConstTransform);
}

void BpMultiplyConstTransform(Bp_Filter_t* filter, Bp_Batch_t** inputs,
                             int n_inputs, Bp_Batch_t* const* outputs, int n_outputs) {
    BpMultiplyConst_t* op = (BpMultiplyConst_t*)filter;
    Bp_Batch_t* in = inputs[0];
    Bp_Batch_t* out = outputs[0];
    
    // Copy batch metadata
    out->head = in->head;
    out->tail = in->tail;
    out->capacity = in->capacity;
    out->t_ns = in->t_ns;
    out->period_ns = in->period_ns;
    out->batch_id = in->batch_id;
    out->ec = in->ec;
    out->meta = in->meta;
    out->dtype = in->dtype;
    
    // Check for pass-through conditions
    if (in->ec != Bp_EC_OK || in->tail <= in->head) {
        return;
    }
    
    size_t n_samples = in->tail - in->head;
    
    // Apply operation based on dtype
    switch (in->dtype) {
        case DTYPE_FLOAT: {
            float* in_data = (float*)in->data + in->head;
            float* out_data = (float*)out->data + out->head;
            for (size_t i = 0; i < n_samples; i++) {
                out_data[i] = in_data[i] * op->scale;
            }
            break;
        }
        case DTYPE_INT: {
            int* in_data = (int*)in->data + in->head;
            int* out_data = (int*)out->data + out->head;
            for (size_t i = 0; i < n_samples; i++) {
                out_data[i] = (int)(in_data[i] * op->scale);
            }
            break;
        }
        case DTYPE_UNSIGNED: {
            unsigned* in_data = (unsigned*)in->data + in->head;
            unsigned* out_data = (unsigned*)out->data + out->head;
            for (size_t i = 0; i < n_samples; i++) {
                out_data[i] = (unsigned)(in_data[i] * op->scale);
            }
            break;
        }
        default:
            SET_FILTER_ERROR(filter, Bp_EC_INVALID_DTYPE, "Unsupported dtype");
            break;
    }
}

/* BpMultiplyMulti Implementation */

Bp_EC BpMultiplyMulti_Init(BpMultiplyMulti_t* op, const BpMultiplyMultiConfig* config) {
    if (!op || !config) {
        return Bp_EC_NULL_FILTER;
    }
    
    // Validate n_inputs from base config
    int n_inputs = config->math_config.base_config.number_of_input_filters;
    if (n_inputs < 2 || n_inputs > MAX_SOURCES) {
        return Bp_EC_INVALID_CONFIG;
    }
    
    // Initialize base filter
    return BpMathOp_InitCommon(&op->base, &config->math_config, BpMultiplyMultiTransform);
}

void BpMultiplyMultiTransform(Bp_Filter_t* filter, Bp_Batch_t** inputs,
                             int n_inputs, Bp_Batch_t* const* outputs, int n_outputs) {
    Bp_Batch_t* out = outputs[0];
    
    // Validate we have the expected number of inputs
    if (n_inputs < 2) {
        SET_FILTER_ERROR(filter, Bp_EC_NOINPUT, "MultiplyMulti requires at least 2 inputs");
        return;
    }
    
    // Use first input as reference for metadata and size
    Bp_Batch_t* first = inputs[0];
    
    // Copy metadata from first input
    out->head = first->head;
    out->tail = first->tail;
    out->capacity = first->capacity;
    out->t_ns = first->t_ns;
    out->period_ns = first->period_ns;
    out->batch_id = first->batch_id;
    out->ec = first->ec;
    out->meta = first->meta;
    out->dtype = first->dtype;
    
    // Check for pass-through conditions
    if (first->ec != Bp_EC_OK || first->tail <= first->head) {
        return;
    }
    
    size_t n_samples = first->tail - first->head;
    
    // Validate all inputs have same size and dtype
    for (int i = 1; i < n_inputs; i++) {
        if (inputs[i]->dtype != first->dtype) {
            SET_FILTER_ERROR(filter, Bp_EC_DTYPE_MISMATCH, "Input dtypes don't match");
            return;
        }
        if ((inputs[i]->tail - inputs[i]->head) != n_samples) {
            SET_FILTER_ERROR(filter, Bp_EC_WIDTH_MISMATCH, "Input sizes don't match");
            return;
        }
    }
    
    // Apply operation based on dtype
    switch (first->dtype) {
        case DTYPE_FLOAT: {
            // Initialize output with first input
            float* out_data = (float*)out->data + out->head;
            float* in0_data = (float*)inputs[0]->data + inputs[0]->head;
            
            for (size_t i = 0; i < n_samples; i++) {
                out_data[i] = in0_data[i];
            }
            
            // Multiply by remaining inputs
            for (int j = 1; j < n_inputs; j++) {
                float* in_data = (float*)inputs[j]->data + inputs[j]->head;
                for (size_t i = 0; i < n_samples; i++) {
                    out_data[i] *= in_data[i];
                }
            }
            break;
        }
        case DTYPE_INT: {
            int* out_data = (int*)out->data + out->head;
            int* in0_data = (int*)inputs[0]->data + inputs[0]->head;
            
            for (size_t i = 0; i < n_samples; i++) {
                out_data[i] = in0_data[i];
            }
            
            for (int j = 1; j < n_inputs; j++) {
                int* in_data = (int*)inputs[j]->data + inputs[j]->head;
                for (size_t i = 0; i < n_samples; i++) {
                    out_data[i] *= in_data[i];
                }
            }
            break;
        }
        case DTYPE_UNSIGNED: {
            unsigned* out_data = (unsigned*)out->data + out->head;
            unsigned* in0_data = (unsigned*)inputs[0]->data + inputs[0]->head;
            
            for (size_t i = 0; i < n_samples; i++) {
                out_data[i] = in0_data[i];
            }
            
            for (int j = 1; j < n_inputs; j++) {
                unsigned* in_data = (unsigned*)inputs[j]->data + inputs[j]->head;
                for (size_t i = 0; i < n_samples; i++) {
                    out_data[i] *= in_data[i];
                }
            }
            break;
        }
        default:
            SET_FILTER_ERROR(filter, Bp_EC_INVALID_DTYPE, "Unsupported dtype");
            break;
    }
}
```

### 3. Test Suite (`tests/test_math_ops.c`)

```c
#include "Unity/unity.h"
#include "bpipe/math_ops.h"
#include <math.h>

// Test fixtures
static BpMultiplyConst_t multiply_const;
static BpMultiplyMulti_t multiply_multi;
static Bp_Filter_t source1, source2, source3;
static Bp_Filter_t sink;

void setUp(void) {
    // Reset all structures
    memset(&multiply_const, 0, sizeof(multiply_const));
    memset(&multiply_multi, 0, sizeof(multiply_multi));
    memset(&source1, 0, sizeof(source1));
    memset(&source2, 0, sizeof(source2));
    memset(&source3, 0, sizeof(source3));
    memset(&sink, 0, sizeof(sink));
}

void tearDown(void) {
    // Cleanup if needed
}

// Helper to create test batch
static Bp_Batch_t create_test_batch(void* data, size_t head, size_t tail, 
                                   SampleDtype_t dtype) {
    Bp_Batch_t batch = {
        .head = head,
        .tail = tail,
        .capacity = 64,
        .t_ns = 1000000,
        .period_ns = 1000,
        .batch_id = 1,
        .ec = Bp_EC_OK,
        .meta = NULL,
        .dtype = dtype,
        .data = data
    };
    return batch;
}

// Test BpMultiplyConst initialization
void test_multiply_const_init_valid(void) {
    BpMultiplyConstConfig config = {
        .math_config = BP_MATH_OP_CONFIG_DEFAULT,
        .value = 2.5f
    };
    config.math_config.base_config.dtype = DTYPE_FLOAT;
    
    Bp_EC ec = BpMultiplyConst_Init(&multiply_const, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, multiply_const.scale);
}

void test_multiply_const_init_null(void) {
    BpMultiplyConstConfig config = {
        .math_config = BP_MATH_OP_CONFIG_DEFAULT,
        .value = 2.5f
    };
    
    Bp_EC ec = BpMultiplyConst_Init(NULL, &config);
    TEST_ASSERT_EQUAL(Bp_EC_NULL_FILTER, ec);
    
    ec = BpMultiplyConst_Init(&multiply_const, NULL);
    TEST_ASSERT_EQUAL(Bp_EC_NULL_FILTER, ec);
}

// Test BpMultiplyConst transform
void test_multiply_const_transform_float(void) {
    // Initialize
    BpMultiplyConstConfig config = {
        .math_config = BP_MATH_OP_CONFIG_DEFAULT,
        .value = 2.0f
    };
    config.math_config.base_config.dtype = DTYPE_FLOAT;
    
    Bp_EC ec = BpMultiplyConst_Init(&multiply_const, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    
    // Create test data
    float input_data[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    float output_data[10] = {0};
    
    Bp_Batch_t input_batch = create_test_batch(input_data, 0, 10, DTYPE_FLOAT);
    Bp_Batch_t output_batch = create_test_batch(output_data, 0, 10, DTYPE_FLOAT);
    
    Bp_Batch_t* inputs[] = {&input_batch};
    Bp_Batch_t* outputs[] = {&output_batch};
    
    // Transform
    BpMultiplyConstTransform(&multiply_const.base, inputs, 1, outputs, 1);
    
    // Verify
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_FLOAT(input_data[i] * 2.0f, output_data[i]);
    }
}

// Test BpMultiplyMulti initialization
void test_multiply_multi_init_valid(void) {
    BpMultiplyMultiConfig config = {
        .math_config = BP_MATH_OP_CONFIG_DEFAULT
    };
    config.math_config.base_config.dtype = DTYPE_FLOAT;
    config.math_config.base_config.number_of_input_filters = 3;
    
    Bp_EC ec = BpMultiplyMulti_Init(&multiply_multi, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
}

void test_multiply_multi_init_invalid_inputs(void) {
    BpMultiplyMultiConfig config = {
        .math_config = BP_MATH_OP_CONFIG_DEFAULT
    };
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
void test_multiply_multi_transform_float(void) {
    // Initialize
    BpMultiplyMultiConfig config = {
        .math_config = BP_MATH_OP_CONFIG_DEFAULT
    };
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
    float expected[5] = {6, 12, 18, 24, 30};  // 1*2*3, 2*2*3, 3*2*3, 4*2*3, 5*2*3
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_FLOAT(expected[i], output[i]);
    }
}

void test_multiply_multi_dtype_mismatch(void) {
    // Initialize
    BpMultiplyMultiConfig config = {
        .math_config = BP_MATH_OP_CONFIG_DEFAULT
    };
    config.math_config.base_config.dtype = DTYPE_FLOAT;
    config.math_config.base_config.number_of_input_filters = 2;
    
    Bp_EC ec = BpMultiplyMulti_Init(&multiply_multi, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    
    // Create mismatched data
    float input1[5] = {1, 2, 3, 4, 5};
    int input2[5] = {2, 2, 2, 2, 2};
    float output[5] = {0};
    
    Bp_Batch_t batch1 = create_test_batch(input1, 0, 5, DTYPE_FLOAT);
    Bp_Batch_t batch2 = create_test_batch(input2, 0, 5, DTYPE_INT);  // Wrong type!
    Bp_Batch_t output_batch = create_test_batch(output, 0, 5, DTYPE_FLOAT);
    
    Bp_Batch_t* inputs[] = {&batch1, &batch2};
    Bp_Batch_t* outputs[] = {&output_batch};
    
    // Transform should set error
    BpMultiplyMultiTransform(&multiply_multi.base, inputs, 2, outputs, 1);
    
    TEST_ASSERT_EQUAL(Bp_EC_DTYPE_MISMATCH, multiply_multi.base.worker_err_info.ec);
}

// Test integer operations
void test_multiply_const_transform_int(void) {
    // Initialize
    BpMultiplyConstConfig config = {
        .math_config = BP_MATH_OP_CONFIG_DEFAULT,
        .value = 3.0f
    };
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

// Main test runner
int main(void) {
    UNITY_BEGIN();
    
    // BpMultiplyConst tests
    RUN_TEST(test_multiply_const_init_valid);
    RUN_TEST(test_multiply_const_init_null);
    RUN_TEST(test_multiply_const_transform_float);
    RUN_TEST(test_multiply_const_transform_int);
    
    // BpMultiplyMulti tests
    RUN_TEST(test_multiply_multi_init_valid);
    RUN_TEST(test_multiply_multi_init_invalid_inputs);
    RUN_TEST(test_multiply_multi_transform_float);
    RUN_TEST(test_multiply_multi_dtype_mismatch);
    
    return UNITY_END();
}
```

### 4. Makefile Updates

Add to existing Makefile:

```makefile
# Math operations source
MATH_OPS_SRC = bpipe/math_ops.c
MATH_OPS_OBJ = $(BUILD_DIR)/math_ops.o

# Update ALL_OBJS
ALL_OBJS += $(MATH_OPS_OBJ)

# Build rule for math_ops
$(MATH_OPS_OBJ): $(MATH_OPS_SRC) bpipe/math_ops.h bpipe/core.h
	$(CC) $(CFLAGS) -c $< -o $@

# Test for math operations
test_math_ops: $(BUILD_DIR)/test_math_ops
	$(BUILD_DIR)/test_math_ops

$(BUILD_DIR)/test_math_ops: tests/test_math_ops.c $(MATH_OPS_OBJ) $(CORE_OBJ) $(UNITY_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Add to test targets
TEST_TARGETS += test_math_ops
```

### 5. Integration Tests

Create `tests/test_math_ops_integration.c`:

```c
#include "Unity/unity.h"
#include "bpipe/math_ops.h"
#include "bpipe/signal_gen.h"

// Test complete pipeline with math operations
void test_pipeline_multiply_const(void) {
    // Create signal generator → multiply → sink pipeline
    Bp_SignalGen_t source;
    BpMultiplyConst_t multiply;
    Bp_Filter_t sink;
    
    // Initialize source
    Bp_EC ec = BpSignalGen_Init(&source, BP_WAVE_SINE, 0.1f, 1.0f, 0.0f, 0.0f,
                                128, 64, 6);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    
    // Initialize multiply
    BpMultiplyConstConfig config = {
        .math_config = BP_MATH_OP_CONFIG_DEFAULT,
        .value = 2.0f
    };
    config.math_config.base_config.dtype = DTYPE_FLOAT;
    
    ec = BpMultiplyConst_Init(&multiply, &config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    
    // Initialize sink
    BpFilterConfig sink_config = BP_FILTER_CONFIG_DEFAULT;
    sink_config.dtype = DTYPE_FLOAT;
    sink_config.transform = BpPassThroughTransform;
    
    ec = BpFilter_Init(&sink, &sink_config);
    TEST_ASSERT_EQUAL(Bp_EC_OK, ec);
    
    // Connect pipeline
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
    usleep(10000);  // 10ms
    
    // Stop filters
    Bp_Filter_Stop(&source.base);
    Bp_Filter_Stop(&multiply.base);
    Bp_Filter_Stop(&sink);
    
    // Verify filters ran without errors
    TEST_ASSERT_EQUAL(Bp_EC_OK, source.base.worker_err_info.ec);
    TEST_ASSERT_EQUAL(Bp_EC_OK, multiply.base.worker_err_info.ec);
    TEST_ASSERT_EQUAL(Bp_EC_OK, sink.worker_err_info.ec);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pipeline_multiply_const);
    return UNITY_END();
}
```

## Implementation Steps

### Day 1: Core Infrastructure
1. Create `bpipe/math_ops.h` with all structures and declarations
2. Implement `BpMathOp_InitCommon` helper function
3. Set up basic test framework

### Day 2: BpMultiplyConst
1. Implement `BpMultiplyConst_Init`
2. Implement `BpMultiplyConstTransform` for all dtypes
3. Write unit tests for initialization and transform

### Day 3: BpMultiplyMulti
1. Implement `BpMultiplyMulti_Init`
2. Implement `BpMultiplyMultiTransform` for all dtypes
3. Write unit tests including error cases

### Day 4: Integration and Testing
1. Update Makefile
2. Create integration tests
3. Test with real pipelines
4. Performance benchmarking

### Day 5: Documentation and Polish
1. Add inline documentation
2. Create usage examples
3. Performance optimization if needed
4. Code review and cleanup

## Success Criteria

1. **Correctness**
   - All unit tests pass
   - Integration tests demonstrate working pipelines
   - Proper error handling for edge cases

2. **Performance**
   - BpMultiplyConst: > 1G samples/sec (single core)
   - BpMultiplyMulti (2 inputs): > 500M samples/sec
   - No memory leaks

3. **Code Quality**
   - Clean separation between operation types
   - Consistent patterns for future operations
   - Well-documented API

4. **Integration**
   - Works with existing filters (SignalGen, etc.)
   - Proper type checking and error propagation
   - Thread-safe operation

## Notes

- Using `base.n_sources` instead of duplicating `n_inputs` field
- Config structures use composition for clean hierarchy
- Transform functions handle batch metadata properly
- Error handling uses existing `SET_FILTER_ERROR` macro
- All operations support float, int, and unsigned types