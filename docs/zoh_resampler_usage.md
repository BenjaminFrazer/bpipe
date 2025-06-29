# Zero-Order Hold Resampler Usage Guide

## Overview

The `BpZOHResampler` is a multi-input synchronization filter that resamples multiple data streams with different sample rates to a common output rate using zero-order hold (sample-and-hold) interpolation.

## Key Features

- **Multi-input synchronization**: Aligns up to `MAX_SOURCES` input streams
- **Fixed output rate**: Generates samples at a user-defined rate
- **Zero-order hold**: Simple and efficient interpolation method
- **Flexible timing**: Handles both regular (fixed-rate) and irregular inputs
- **Batch processing**: Efficient processing of data in batches
- **Underrun handling**: Configurable behavior when inputs lack data

## Basic Usage

### 1. Single Input Resampling

```c
// Create a resampler that outputs at 1 kHz
BpZOHResampler_t resampler;
Bp_EC ec = BpZOHResampler_InitSimple(&resampler, 1000, 1, DTYPE_FLOAT);

// Connect source and sink
Bp_add_source(&resampler.base, &source);
Bp_add_sink(&resampler.base, &sink);

// Start processing
Bp_Filter_Start(&resampler.base);
```

### 2. Multi-Input Synchronization

```c
// Configure resampler for 3 inputs at 500 Hz output
BpZOHResamplerConfig config = BP_ZOH_RESAMPLER_CONFIG_DEFAULT;
config.output_period_ns = 2000000;  // 2ms = 500 Hz
config.base_config.number_of_input_filters = 3;
config.base_config.dtype = DTYPE_FLOAT;

BpZOHResampler_t resampler;
Bp_EC ec = BpZOHResampler_Init(&resampler, &config);

// Connect three sources
Bp_add_source(&resampler.base, &source_1000hz);
Bp_add_source(&resampler.base, &source_1200hz);  
Bp_add_source(&resampler.base, &source_100hz);
```

## Configuration Options

### BpZOHResamplerConfig

| Field | Description | Default |
|-------|-------------|---------|
| `output_period_ns` | Output sample period in nanoseconds | 1000000 (1ms) |
| `drop_on_underrun` | Drop output samples if inputs missing | false |
| `max_input_buffer` | Samples to store per input | 1 |
| `base_config` | Standard filter configuration | See BpFilterConfig |

### Timing Configuration

Output rate can be specified in Hz or as a period in nanoseconds:

```c
// Using Hz (more intuitive)
BpZOHResampler_InitSimple(&resampler, 1000, n_inputs, dtype);  // 1 kHz

// Using period_ns (more precise)
config.output_period_ns = 1000000;  // 1,000,000 ns = 1 ms = 1 kHz
```

## Output Format

### Single Input
For single input resampling, output samples are in the same format as the input.

### Multiple Inputs
For multi-input synchronization, output samples are **interleaved**:
```
[input0_sample0, input1_sample0, input2_sample0, 
 input0_sample1, input1_sample1, input2_sample1, ...]
```

## Timing Behavior

### Zero-Order Hold Algorithm

The resampler uses the last known value from each input when generating output samples:

1. For each output sample time `t_out`:
   - Use the most recent input sample where `t_input <= t_out`
   - If no data available yet, output zero or hold previous value

### Example Timeline

```
Input (100 Hz):  |--A--|--B--|--C--|--D--|
Output (250 Hz): |-A-|-A-|-B-|-B-|-C-|-C-|-C-|-D-|
```

## Underrun Handling

When an input doesn't have data for the current output time:

- **`drop_on_underrun = false`** (default): Use last known value (or zero if no data yet)
- **`drop_on_underrun = true`**: Skip output sample entirely

## Performance Considerations

1. **Batch Processing**: The resampler processes entire batches at once for efficiency
2. **Memory Usage**: Only stores one sample per input (for ZOH)
3. **CPU Usage**: Linear with number of output samples generated
4. **Latency**: Minimal - outputs generated as soon as input data available

## Limitations

### Current Implementation

1. **Batch-based ZOH**: The current implementation only keeps the last sample from each input batch, which may not provide true sample-level ZOH for upsampling scenarios.

2. **No Advanced Interpolation**: Only zero-order hold is supported. Linear or cubic interpolation would require a different filter.

3. **Fixed Output Rate**: The output rate is fixed at initialization time.

## Statistics and Monitoring

Get runtime statistics for each input:

```c
BpResamplerInputStats_t stats;
BpZOHResampler_GetInputStats(&resampler, input_idx, &stats);

printf("Input %d: samples=%llu, underruns=%llu (%.1f%%)\n",
       input_idx, stats.samples_processed, 
       stats.underrun_count, stats.underrun_percentage);
```

## Common Use Cases

### 1. Rate Conversion
Convert a 48 kHz audio stream to 44.1 kHz:
```c
BpZOHResampler_InitSimple(&resampler, 44100, 1, DTYPE_FLOAT);
```

### 2. Sensor Fusion
Combine sensors with different update rates:
- GPS: 10 Hz
- IMU: 100 Hz  
- Magnetometer: 50 Hz
→ Fused output: 100 Hz

### 3. Data Alignment
Align multiple data streams for synchronized processing:
- Camera frames: 30 Hz
- Lidar scans: 10 Hz
- Control inputs: 100 Hz
→ Synchronized output: 30 Hz

## Best Practices

1. **Choose Output Rate Wisely**: 
   - At least as fast as the slowest input to avoid data loss
   - Consider the Nyquist rate for your signals

2. **Monitor Underruns**: 
   - Check statistics to ensure inputs are keeping up
   - Adjust buffer sizes if needed

3. **Consider Batch Sizes**:
   - Larger batches are more efficient but increase latency
   - Balance based on your application needs

4. **Type Consistency**:
   - All inputs must have the same data type (float, int, etc.)
   - Type conversions should be done before resampling

## Example Code

See `examples/multi_rate_sync.c` for a complete working example demonstrating multi-rate synchronization with three different input rates.