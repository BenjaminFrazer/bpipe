#!/bin/bash
# Build script for csv_to_csv_scale example

# Ensure build directory exists
mkdir -p ../build

# List of required object files (excluding test files)
OBJECTS="
../build/batch_buffer.o
../build/batch_buffer_print.o
../build/batch_matcher.o
../build/bperr.o
../build/core.o
../build/csv_sink.o
../build/csv_source.o
../build/map.o
../build/map_examples.o
../build/sample_aligner.o
../build/signal_generator.o
../build/tee.o
"

# Compile the example
gcc -std=c99 -Wall -Werror -pthread \
    -I../bpipe \
    -I../lib/Unity/src \
    csv_to_csv_scale.c \
    $OBJECTS \
    -lm \
    -o ../build/csv_to_csv_scale

if [ $? -eq 0 ]; then
    echo "Build successful! Run with:"
    echo "  ../build/csv_to_csv_scale <input.csv> <output.csv> <scale_factor>"
    echo ""
    echo "Example:"
    echo "  ../build/csv_to_csv_scale data/sensor_data.csv data/scaled_output.csv 2.0"
else
    echo "Build failed!"
    exit 1
fi