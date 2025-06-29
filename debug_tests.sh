#!/bin/bash

# Script to debug which test is causing the hang

tests=(
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
    "test_Await_Timeout_Behavior"
    "test_Await_Stopped_Behavior"
)

echo "Running tests individually..."
for test in "${tests[@]}"; do
    echo "Testing: $test"
    if ! ./run_with_timeout.sh 10 ./build/test_core_filter "$test" >/dev/null 2>&1; then
        echo "FAILED: $test"
        exit 1
    else
        echo "PASSED: $test"
    fi
done

echo "All individual tests passed. The issue is in running them together."