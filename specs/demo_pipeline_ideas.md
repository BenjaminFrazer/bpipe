# Demo Pipeline Ideas

This document contains pipeline demo ideas to validate the pipeline filter concept, organized by complexity and available components.

## Using Existing Components

Available filters:
- `signal_generator` - Source filter
- `map` - Processing filter (can scale, offset, etc.)
- `tee` - Fan-out filter
- `csv_sink` - Sink filter
- `debug_output` - Monitoring filter

## Simple Demo: Signal Processing + Monitoring

**Concept**: Generate signal, process it, monitor at multiple points, output to CSV

```c
Pipeline_t* create_signal_demo() {
    Pipeline_t* pipe = pipeline_create("signal_demo");
    
    // Internal topology:
    // signal_gen -> scale -> tee -> [csv_sink, debug_output]
    
    pipeline_add_filter(pipe, "scaler", map_create(scale_by_2));
    pipeline_add_filter(pipe, "tee", tee_create(2));
    
    // Connect internally
    pipeline_connect(pipe, "scaler", 0, "tee", 0);
    
    // Expose interface
    pipeline_expose_input(pipe, "scaler", 0, 0);     // Signal input
    pipeline_expose_output(pipe, "tee", 0, 0);       // Output 0: To CSV
    pipeline_expose_output(pipe, "tee", 1, 1);       // Output 1: To debug
    
    return pipe;
}

// Usage
SignalGenerator_t source;
Pipeline_t* processor = create_signal_demo();
CsvSink_t csv_out;
DebugOutput_t debug_out;

// Connect: source -> pipeline -> [csv, debug]
filt_connect(&source, 0, processor->input_buffers[0]);
filt_connect(processor, 0, csv_out.input_buffers[0]);
filt_connect(processor, 1, debug_out.input_buffers[0]);
```

**Validation**:
- Pipeline encapsulates 2 filters (map + tee) 
- Presents 1-in, 2-out interface
- Zero-copy buffer sharing
- Standard connection API works

## Multi-Stage Processing Demo

**Concept**: Chain multiple processing stages inside pipeline

```c
Pipeline_t* create_multi_stage() {
    Pipeline_t* pipe = pipeline_create("multi_stage");
    
    // signal -> gain -> offset -> scale -> output
    pipeline_add_filter(pipe, "gain", map_create(multiply_by_10));
    pipeline_add_filter(pipe, "offset", map_create(add_offset_5));
    pipeline_add_filter(pipe, "scale", map_create(scale_by_half));
    
    // Linear chain
    pipeline_connect(pipe, "gain", 0, "offset", 0);
    pipeline_connect(pipe, "offset", 0, "scale", 0);
    
    // Simple 1-in, 1-out
    pipeline_expose_input(pipe, "gain", 0, 0);
    pipeline_expose_output(pipe, "scale", 0, 0);
    
    return pipe;
}
```

**Validation**:
- Sequential processing chain
- Multiple internal connections
- Simple external interface
- Performance comparison vs manual chaining

## Fan-Out + Fan-In Demo (Requires Join)

**Concept**: Split signal, process differently, recombine
*Note: Requires implementing a simple join/combiner filter*

```c
Pipeline_t* create_split_merge() {
    Pipeline_t* pipe = pipeline_create("split_merge");
    
    // input -> tee -> [path1, path2] -> combiner -> output
    pipeline_add_filter(pipe, "split", tee_create(2));
    pipeline_add_filter(pipe, "path1", map_create(scale_by_2));
    pipeline_add_filter(pipe, "path2", map_create(add_offset_10));
    pipeline_add_filter(pipe, "combine", simple_add_filter_create()); // A+B
    
    pipeline_connect(pipe, "split", 0, "path1", 0);
    pipeline_connect(pipe, "split", 1, "path2", 0);
    pipeline_connect(pipe, "path1", 0, "combine", 0);
    pipeline_connect(pipe, "path2", 0, "combine", 1);
    
    pipeline_expose_input(pipe, "split", 0, 0);
    pipeline_expose_output(pipe, "combine", 0, 0);
}
```

## Recommended Starting Point: Multi-Stage Processing

**Why this one first:**
1. **Uses only existing components** (signal_gen, map, csv_sink)
2. **Simple topology** - linear chain
3. **Clear validation** - can compare output vs manual chaining
4. **Demonstrates key benefits**:
   - Encapsulation of complex processing
   - Zero-copy operation
   - Standard API compatibility

**Test scenario:**
```c
void test_multi_stage_pipeline() {
    // Create components
    SignalGenerator_t gen;
    signal_generator_init(&gen, sine_wave_config);
    
    Pipeline_t* processor = create_multi_stage();
    
    CsvSink_t sink;
    csv_sink_init(&sink, output_config);
    
    // Connect like any regular filter
    filt_connect(&gen, 0, processor->input_buffers[0]);
    filt_connect(processor, 0, sink.input_buffers[0]);
    
    // Run and verify output = input * 10 + 5 * 0.5 = input * 2.5
    run_test();
}
```

## Future Ideas (Need Additional Components)

### Auto-Scaling Pipeline
- Requires: statistics filter, delay line, parametric scaler
- **Function**: Automatically normalize signal to [0,1] range

### Signal Quality Monitor  
- Requires: SNR calculator, clip detector
- **Function**: Monitor signal quality while passing data through

### DC Offset Removal
- Requires: moving average, subtract filter
- **Function**: Remove DC component using high-pass approach

### Stereo Processor
- Requires: channel splitter/combiner
- **Function**: Process L/R channels independently

### Frequency Domain Processing
- Requires: FFT, IFFT filters
- **Function**: FFT -> process -> IFFT pipeline

## Implementation Priority

1. **Multi-Stage Processing** - Uses existing components
2. **Signal + Monitoring** - Demonstrates fan-out
3. **Split-Merge** - After implementing simple combiner
4. **Advanced pipelines** - As more components become available

## Validation Criteria

Each demo should verify:
- [ ] Pipeline creates successfully
- [ ] Internal connections work
- [ ] External interface matches specification
- [ ] Data flows correctly end-to-end
- [ ] Performance comparable to manual implementation
- [ ] Memory usage reasonable
- [ ] Cleanup works properly
- [ ] Can be used like any other filter