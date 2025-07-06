#!/bin/bash

# Simple timeout wrapper for test executables
# Usage: ./run_with_timeout.sh <timeout_seconds> <executable> [args...]
#
# Exit codes:
#   0   - Success
#   1   - Test failure
#   124 - Timeout (standard timeout command exit code)
#   2   - Script usage error

# Check arguments
if [ $# -lt 2 ]; then
    echo "Usage: $0 <timeout_seconds> <executable> [args...]" >&2
    echo "Example: $0 30 ./build/test_core_filter" >&2
    exit 2
fi

TIMEOUT_SECONDS=$1
EXECUTABLE=$2
shift 2  # Remove timeout and executable from arguments
ARGS="$@"

# Validate timeout is a number
if ! [[ "$TIMEOUT_SECONDS" =~ ^[0-9]+$ ]]; then
    echo "Error: timeout must be a positive integer (got: $TIMEOUT_SECONDS)" >&2
    exit 2
fi

# Check if executable exists and is executable
if [ ! -x "$EXECUTABLE" ]; then
    echo "Error: $EXECUTABLE is not executable or does not exist" >&2
    exit 2
fi

# Log file for timeout events
LOG_FILE="timeout.log"
TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

# Function to log timeout events
log_timeout() {
    echo "[$TIMESTAMP] TIMEOUT: $EXECUTABLE $ARGS (${TIMEOUT_SECONDS}s timeout exceeded)" >> "$LOG_FILE"
}

# Function to log successful runs (for debugging)
log_success() {
    echo "[$TIMESTAMP] SUCCESS: $EXECUTABLE $ARGS (completed within ${TIMEOUT_SECONDS}s)" >> "$LOG_FILE"
}

# Run the command with timeout
echo "Running: $EXECUTABLE $ARGS (timeout: ${TIMEOUT_SECONDS}s)"

# Use timeout command with SIGTERM, then SIGKILL after 5 seconds
if timeout --preserve-status --kill-after=5 "$TIMEOUT_SECONDS" "$EXECUTABLE" $ARGS; then
    # Success case
    log_success
    exit 0
else
    EXIT_CODE=$?
    if [ $EXIT_CODE -eq 124 ]; then
        # Timeout case
        echo "ERROR: Test timed out after ${TIMEOUT_SECONDS} seconds" >&2
        log_timeout
        exit 124
    else
        # Test failure case
        echo "Test failed with exit code: $EXIT_CODE" >&2
        exit $EXIT_CODE
    fi
fi

