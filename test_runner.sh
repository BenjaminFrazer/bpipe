#!/bin/bash
# Run tests one by one with timeout to find which is hanging

TESTS=(
    "test_BpFilter_Init_Success"
    "test_BpFilter_Init_Failure" 
    "test_Bp_add_sink_Success"
    "test_Bp_add_multiple_sinks"
    "test_Bp_remove_sink_Success"
    "test_multi_transform_function"
    "test_Bp_Filter_Start_Success"
    "test_Bp_Filter_Start_Already_Running"
    "test_Bp_Filter_Stop_Success"
    "test_Bp_Filter_Stop_Not_Running"
    "test_Bp_Filter_Start_Null_Filter"
    "test_Bp_Filter_Stop_Null_Filter"
    "test_Overflow_Behavior_Block_Default"
    "test_Overflow_Behavior_Drop_Mode"
)

for test in "${TESTS[@]}"; do
    echo "Running $test..."
    if timeout 2 ./build/test_core_filter "$test" > /dev/null 2>&1; then
        echo "  ✓ PASSED"
    else
        echo "  ✗ FAILED or TIMEOUT"
        echo "  Found problematic test: $test"
        break
    fi
done