#!/bin/bash

# Quiet test runner - suppresses verbose output to avoid SIGPIPE/SIGTERM issues
# Usage: ./run_test_quiet.sh <timeout_seconds> <executable> [args...]
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

# Create temporary file for output
TEMP_OUTPUT=$(mktemp)
trap "rm -f $TEMP_OUTPUT" EXIT

# Run the command with timeout, capturing output
echo -n "Running $(basename $EXECUTABLE)... "

# Use timeout command with SIGTERM, then SIGKILL after 5 seconds
if timeout --preserve-status --kill-after=5 "$TIMEOUT_SECONDS" "$EXECUTABLE" $ARGS > "$TEMP_OUTPUT" 2>&1; then
    # Success case
    echo "PASS"
    log_success
    exit 0
else
    EXIT_CODE=$?
    if [ $EXIT_CODE -eq 124 ]; then
        # Timeout case
        echo "TIMEOUT"
        echo "ERROR: Test timed out after ${TIMEOUT_SECONDS} seconds" >&2
        echo "Last 50 lines of output:" >&2
        tail -50 "$TEMP_OUTPUT" >&2
        log_timeout
        exit 124
    else
        # Test failure case
        echo "FAIL (exit code: $EXIT_CODE)"
        echo "Last 100 lines of output:" >&2
        tail -100 "$TEMP_OUTPUT" >&2
        exit $EXIT_CODE
    fi
fi