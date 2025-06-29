# Sawtooth Demo Specification

## Overview

The sawtooth demo is a real-time visualization tool that demonstrates the core capabilities of the bpipe framework. It creates a three-stage pipeline (signal → passthrough → plot) that runs continuously, displaying live data flow to verify system integrity.

## Purpose

1. **System Verification**: Validate that all core components work together correctly
2. **Visual Integrity Check**: Sawtooth waveform makes data corruption immediately visible
3. **Usage Example**: Demonstrate best practices for building bpipe pipelines
4. **Performance Testing**: Show real-time streaming capabilities

## Requirements

### Functional Requirements

1. **Pipeline Structure**
   - Source: Sawtooth signal generator
   - Transform: Passthrough filter (data integrity verification)
   - Sink: Real-time plotting sink

2. **Real-time Visualization**
   - Live updating plot that shows data as it flows
   - Continuous operation until user interrupts (Ctrl+C)
   - Smooth animation without flickering

3. **Data Integrity**
   - Clean sawtooth pattern with no glitches
   - Consistent amplitude and frequency
   - No data drops or corruption

4. **User Interface**
   - Clear plot with grid and labels
   - Display current metrics (samples/sec, buffer status)
   - Responsive to window resize
   - Clean shutdown on interrupt

### Non-functional Requirements

1. **Performance**
   - Minimal CPU usage for visualization
   - No memory leaks during extended runs
   - Smooth 30+ FPS plot updates

2. **Code Quality**
   - Concise and readable implementation
   - No try/catch blocks (per project standards)
   - Clear comments explaining pipeline setup

3. **Robustness**
   - Graceful handling of slow consumers
   - Clean shutdown without hanging
   - No zombie threads

## Design

### Component Architecture

```
┌─────────────────┐     ┌──────────────┐     ┌─────────────────┐
│ SawtoothSource  │────>│ Passthrough  │────>│ RealtimePlot    │
│                 │     │              │     │                 │
│ - frequency     │     │ - validates  │     │ - live display  │
│ - amplitude     │     │   data flow  │     │ - metrics       │
│ - sample_rate   │     │              │     │ - auto-scaling  │
└─────────────────┘     └──────────────┘     └─────────────────┘
```

### Signal Parameters

- **Frequency**: 0.01 Hz (1 cycle per 100 samples)
- **Amplitude**: 1.0 (range: -1 to +1)
- **Sample Rate**: ~1000 samples/second
- **Batch Size**: 64 samples

### Plot Configuration

- **Window Size**: 1000 samples (1 second of data)
- **Update Rate**: 30 Hz
- **Y-axis Range**: -1.5 to +1.5 (auto-scaling optional)
- **Grid**: Enabled with 0.3 alpha

### Implementation Approach

1. **Use matplotlib.animation.FuncAnimation** for real-time updates
2. **Ring buffer in plot sink** to maintain fixed window size
3. **Non-blocking plot updates** to prevent pipeline stalls
4. **Signal handler** for clean Ctrl+C shutdown

## User Experience

### Startup
```
$ python demos/sawtooth_demo.py
Starting sawtooth demo pipeline...
Press Ctrl+C to stop

Pipeline running:
- Signal: 1000 samples/sec @ 0.01 Hz
- Buffer: 0% full
- Display: 30 FPS
```

### Runtime Display
- Live sawtooth waveform scrolling across plot
- Status bar showing:
  - Current sample rate
  - Buffer utilization
  - Frame rate
  - Total samples processed

### Shutdown
```
^C
Stopping pipeline...
- Processed 145,236 samples
- Average rate: 1001.2 samples/sec
- No data corruption detected
Pipeline stopped cleanly.
```

## Success Criteria

1. **Visual Quality**
   - Smooth, continuous sawtooth pattern
   - No visible glitches or discontinuities
   - Responsive plot updates

2. **Performance**
   - Maintains target sample rate
   - Plot updates don't block pipeline
   - Low CPU usage (<10% single core)

3. **Reliability**
   - Runs indefinitely without issues
   - Clean shutdown every time
   - No resource leaks

4. **Code Quality**
   - Under 150 lines of code
   - Clear structure and comments
   - Follows project conventions

## Edge Cases

1. **Slow Display**: Plot updates lag behind data generation
   - Solution: Skip frames, maintain data integrity

2. **Window Resize**: User changes plot window size
   - Solution: Adapt visualization, maintain aspect ratio

3. **System Load**: High CPU usage from other processes
   - Solution: Degrade gracefully, prioritize data flow

## Testing Considerations

1. **Long Duration**: Run for 1+ hours to verify stability
2. **Interruption**: Test Ctrl+C at various points
3. **Performance**: Monitor CPU and memory usage
4. **Visual Verification**: Check for data corruption patterns

## Future Enhancements

1. **Parameter Controls**: Runtime adjustment of frequency/amplitude
2. **Multiple Signals**: Demonstrate multi-input capabilities
3. **Performance Metrics**: Detailed throughput analysis
4. **Save/Load**: Record and replay sessions

## Conclusion

This demo serves as both a validation tool and a usage example. Its real-time visualization makes it ideal for:
- Verifying framework functionality
- Demonstrating proper pipeline construction
- Debugging data flow issues
- Performance benchmarking

The sawtooth waveform's linear nature makes any data corruption immediately visible, fulfilling the goal of a low-maintenance, high-coverage test that exercises all core features.