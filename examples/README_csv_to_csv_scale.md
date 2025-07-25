# CSV to CSV Scale Example

This example demonstrates a complete data processing pipeline that:
1. Reads time-series data from a CSV file using `CsvSource_t`
2. Scales all data values using a `Map_t` filter
3. Writes the processed data to a new CSV file using `CSVSink_t`

## Building

```bash
# From the examples directory
./build_csv_to_csv_scale.sh
```

## Running

```bash
# Usage
../build/csv_to_csv_scale <input.csv> <output.csv> <scale_factor>

# Example: Scale all values by 2.0
../build/csv_to_csv_scale data/sensor_data.csv data/scaled_output.csv 2.0

# Example: Convert Celsius to Fahrenheit (scale by 1.8 and offset by 32)
# Note: This example uses scale only, you'd need to modify the code to add offset
../build/csv_to_csv_scale data/sensor_data.csv data/fahrenheit_output.csv 1.8
```

## Input Format

The input CSV should have:
- A timestamp column (named `timestamp_ns` by default)
- One or more data columns with numeric values
- Header row with column names

Example input (`data/sensor_data.csv`):
```csv
timestamp_ns,temperature,pressure,humidity
1000000000,25.5,1013.25,65.2
1001000000,25.6,1013.24,65.0
1002000000,25.7,1013.23,64.8
```

## Output Format

The output CSV will contain:
- The same timestamps as the input
- All data values scaled by the specified factor
- Column names prefixed with "scaled_"

Example output with scale factor 2.0:
```csv
timestamp_ns,scaled_ch0,scaled_ch1,scaled_ch2
1000000000,51.000,2026.500,130.400
1001000000,51.200,2026.480,130.000
1002000000,51.400,2026.460,129.600
```

## Pipeline Architecture

```
┌─────────────┐         ┌─────────────┐         ┌─────────────┐
│  CsvSource  │         │     Map     │         │   CsvSink   │
│             │ push to │   (scale)   │ push to │             │
│  [output]---┼────────>│ [input][out]┼────────>│  [input]    │
│             │         │             │         │             │
└─────────────┘         └─────────────┘         └─────────────┘
```

## Key Features Demonstrated

1. **CSV Source Configuration**:
   - Automatic column detection
   - Regular timing detection
   - Header parsing
   - Configurable batch sizes

2. **Map Filter**:
   - Multi-channel processing
   - Configurable scale factor
   - Efficient batch processing

3. **CSV Sink Configuration**:
   - Multi-column output format
   - Custom column names
   - Configurable precision
   - Header generation

4. **Error Handling**:
   - Proper initialization checking
   - Worker thread error reporting
   - Graceful cleanup on failure

5. **Memory Management**:
   - Dynamic column name allocation
   - Proper cleanup in all code paths
   - Filter destruction in correct order