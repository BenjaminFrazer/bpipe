#!/bin/bash
# Run a test with timeout to prevent hanging

TEST_PROGRAM=$1
TIMEOUT=${2:-5}  # Default 5 seconds

if [ -z "$TEST_PROGRAM" ]; then
    echo "Usage: $0 <test_program> [timeout_seconds]"
    exit 1
fi

echo "Running $TEST_PROGRAM with ${TIMEOUT}s timeout..."
timeout --preserve-status $TIMEOUT $TEST_PROGRAM
EXIT_CODE=$?

if [ $EXIT_CODE -eq 124 ]; then
    echo "ERROR: Test timed out after ${TIMEOUT}s"
    exit 1
elif [ $EXIT_CODE -ne 0 ]; then
    echo "ERROR: Test failed with exit code $EXIT_CODE"
    exit $EXIT_CODE
else
    echo "Test completed successfully"
fi