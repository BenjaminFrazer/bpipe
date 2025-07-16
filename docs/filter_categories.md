# Filter categories

## Overview

This document describes the fundimental types of filters.

## Core Philosophy

### Current Design Problems
1. The generic `Bp_Worker` tries to abstract away details that complex filters actually need
2. Simple filters are forced to deal with unnecessary complexity (multi-input/output arrays, batch management)
3. The transform function signature is awkward - passing arrays of batches when most filters only use one
4. Corner cases in multi-input/output scenarios challenge the specificity of the transform function's role

### New Design Principles
1. **Each filter owns its execution model** - The filter's worker function has complete control
2. **Simplicity by default** - Common patterns (like element-wise mapping) have simple APIs
3. **Power when needed** - Complex filters can implement custom synchronization, timing, and routing
4. **No forced abstractions** - Filters aren't required to fit into a one-size-fits-all execution model

## Architecture

### Core Filter Structure
```c
typedef void* (*BpWorkerFunc)(void* filter);

typedef struct Bp_Filter_s {
    char name[MAX_FILTER_NAME];
    BpWorkerFunc worker;        // Direct worker function, not transform
    void* context;              // User data/function pointer
    pthread_t worker_thread;
    bool running;
    
    // Data properties
    Dtype_t dtype;
    size_t data_width;
    
    // Connections
    Bp_BatchBuffer_t input_buffers[MAX_SOURCES];
    Bp_Filter_t* sinks[MAX_SINKS];
    int n_input_buffers;
    int n_sinks;
    
    // Synchronization
    pthread_mutex_t filter_mutex;
    
    // Error handling
    Bp_EC last_error;
    char error_message[256];
} Bp_Filter_t;
```

### Worker Function Categories

#### 1. Map Worker (Simple Element-wise Operations)
For filters that process data element-by-element without complex synchronization needs:

```c
typedef void (*BpMapFunc)(const void* input, void* output, size_t n_samples);

void* BpMapWorker(void* filter_ptr) {
    Bp_Filter_t* f = (Bp_Filter_t*)filter_ptr;
    BpMapFunc map_func = (BpMapFunc)f->context;
    
    // Simple single-input, single-output processing
    while (f->running) {
        // Get available data
        // Call map function
        // Manage batch transitions
    }
    return NULL;
}
```

#### 2. Generic Worker (Current Bp_Worker)
For filters that need the full multi-input/output infrastructure:

```c
void* BpGenericWorker(void* filter_ptr) {
    // Current Bp_Worker implementation
    // Handles multiple inputs/outputs
    // Automatic distribution to multiple sinks
    // Complex batch management
}
```

#### 3. Stateful Map Worker (State-Preserving Element-wise Operations)
For filters that need to maintain state between processing iterations:

```c
typedef void (*BpStatefulMapFunc)(const void* input, void* output, 
                                  size_t n_samples, void* state);

void* BpStatefulMapWorker(void* filter_ptr) {
    Bp_Filter_t* f = (Bp_Filter_t*)filter_ptr;
    
    // Context holds both the map function and the state
    typedef struct {
        BpStatefulMapFunc map_func;
        void* state;
    } StatefulMapContext;
    
    StatefulMapContext* ctx = (StatefulMapContext*)f->context;
    
    while (f->running) {
        // Get input batch
        Bp_Batch_t* in_batch = Bp_get_in_batch(f, &f->input_buffers[0], true);
        if (!in_batch) continue;
        
        // Allocate output batch
        Bp_Batch_t out_batch = Bp_allocate(f->sinks[0], 
                                           &f->sinks[0]->input_buffers[0]);
        
        // Process with state
        ctx->map_func(in_batch->data, out_batch.data, 
                      in_batch->head, ctx->state);
        
        // Submit output
        out_batch.head = in_batch->head;
        out_batch.p_tag = in_batch->p_tag;
        Bp_submit_batch(f->sinks[0], &f->sinks[0]->input_buffers[0], &out_batch);
        
        // Advance input
        Bp_advance(f, &f->input_buffers[0]);
    }
    return NULL;
}
```

#### 4. Function Generator Worker (Arbitrary Waveform Generation)
For source filters that generate arbitrary waveforms based on time:

```c
typedef void (*BpGeneratorFunc)(void* output, size_t n_samples, 
                                double start_time, void* params);

void* BpFunctionGeneratorWorker(void* filter_ptr) {
    Bp_Filter_t* f = (Bp_Filter_t*)filter_ptr;
    
    typedef struct {
        BpGeneratorFunc gen_func;
        void* params;
        double current_time;
        double sample_rate;
    } GeneratorContext;
    
    GeneratorContext* ctx = (GeneratorContext*)f->context;
    
    while (f->running) {
        // Allocate output batch
        Bp_Batch_t out_batch = Bp_allocate(f->sinks[0], 
                                           &f->sinks[0]->input_buffers[0]);
        
        // Generate samples starting from current time
        ctx->gen_func(out_batch.data, out_batch.capacity, 
                      ctx->current_time, ctx->params);
        
        // Update time for next iteration
        ctx->current_time += out_batch.capacity / ctx->sample_rate;
        
        // Submit batch
        out_batch.head = out_batch.capacity;
        out_batch.p_tag = (Bp_Tag_t){
            .sample_count = out_batch.capacity,
            .time_stamp = ctx->current_time
        };
        
        Bp_submit_batch(f->sinks[0], &f->sinks[0]->input_buffers[0], &out_batch);
    }
    return NULL;
}
```

#### 5. Custom Workers
For specialized filters with unique requirements:

```c
void* CustomSyncWorker(void* filter_ptr) {
    Bp_Filter_t* f = (Bp_Filter_t*)filter_ptr;
    
    // Custom synchronization logic
    // Direct buffer access
    // Specialized timing requirements
    // Custom error handling
}
```

## Implementation Patterns

### Creating Filters

#### Simple Map Filter
```c
// User writes this simple function
void scale_samples(const float* in, float* out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        out[i] = in[i] * 2.0f;
    }
}

// Create filter
BpFilterConfig config = BP_CONFIG_FLOAT_STANDARD;
config.worker = BpMapWorker;
Bp_Filter_t* scaler = BpFilter_Create(&config);
scaler->context = scale_samples;
```

#### Stateful Map Filter
```c
// User writes stateful processing function
typedef struct {
    float gain;
    float offset;
} GainOffsetState;

void gain_offset_process(const float* in, float* out, size_t n, void* state) {
    GainOffsetState* s = (GainOffsetState*)state;
    for (size_t i = 0; i < n; i++) {
        out[i] = in[i] * s->gain + s->offset;
    }
}

// Create filter
BpFilterConfig config = BP_CONFIG_FLOAT_STANDARD;
config.worker = BpStatefulMapWorker;
Bp_Filter_t* processor = BpFilter_Create(&config);

// Set up state
GainOffsetState* state = malloc(sizeof(GainOffsetState));
state->gain = 2.0f;
state->offset = 1.0f;

typedef struct {
    BpStatefulMapFunc map_func;
    void* state;
} StatefulMapContext;

StatefulMapContext* ctx = malloc(sizeof(StatefulMapContext));
ctx->map_func = gain_offset_process;
ctx->state = state;

processor->context = ctx;
```

#### Function Generator Filter
```c
// User writes generation function
void sine_generator(void* output, size_t n_samples, double start_time, void* params) {
    float* out = (float*)output;
    float* freq = (float*)params;
    double dt = 1.0 / 48000.0;  // Sample rate
    
    for (size_t i = 0; i < n_samples; i++) {
        double t = start_time + i * dt;
        out[i] = sinf(2.0f * M_PI * (*freq) * t);
    }
}

// Create generator
BpFilterConfig config = BP_CONFIG_FLOAT_STANDARD;
config.worker = BpFunctionGeneratorWorker;
config.number_of_input_buffers = 0;  // Source

Bp_Filter_t* sine_gen = BpFilter_Create(&config);

// Set up generator context
typedef struct {
    BpGeneratorFunc gen_func;
    void* params;
    double current_time;
    double sample_rate;
} GeneratorContext;

float* frequency = malloc(sizeof(float));
*frequency = 440.0f;  // A4

GeneratorContext* ctx = malloc(sizeof(GeneratorContext));
ctx->gen_func = sine_generator;
ctx->params = frequency;
ctx->current_time = 0.0;
ctx->sample_rate = 48000.0;

sine_gen->context = ctx;
```

#### Complex Multi-Input Filter
```c
void* CrossCorrelationWorker(void* filter_ptr) {
    Bp_Filter_t* f = (Bp_Filter_t*)filter_ptr;
    
    // Direct access to multiple input buffers
    // Custom synchronization between inputs
    // Complex output generation
}

BpFilterConfig config = {
    .worker = CrossCorrelationWorker,
    .number_of_input_buffers = 2,
    // ... other config
};
```

### Filter Lifecycle

```c
Bp_EC Bp_Filter_Start(Bp_Filter_t* filter) {
    if (!filter || !filter->worker) {
        return Bp_EC_NULL_FILTER;
    }
    
    filter->running = true;
    
    if (pthread_create(&filter->worker_thread, NULL, 
                       filter->worker, filter) != 0) {
        filter->running = false;
        return Bp_EC_THREAD_CREATE_FAIL;
    }
    
    return Bp_EC_OK;
}
```

## Benefits

### For Simple Use Cases
- No need to understand batch structures
- No multi-input/output complexity
- Just provide a function that processes arrays
- Framework handles all buffer management

### For Stateful Processing
- Clean separation of state from processing logic
- State automatically preserved between batches
- No need to manage filter context directly
- Perfect for IIR filters, running statistics, control loops

### For Signal Generation
- Time-based generation with automatic time tracking
- Clean interface for arbitrary waveform synthesis
- No batch management complexity
- Consistent timing across batch boundaries

### For Complex Use Cases
- Full access to filter structure
- Direct buffer manipulation
- Custom synchronization strategies
- Specialized error handling

### For Framework Maintenance
- Cleaner separation of concerns
- Each worker type can evolve independently
- Easier to add new patterns
- Less "one size fits all" compromise

## Migration Strategy

1. **Phase 1**: Introduce worker function to filter structure alongside transform
2. **Phase 2**: Implement BpMapWorker and migrate simple filters
3. **Phase 3**: Rename current Bp_Worker to BpGenericWorker
4. **Phase 4**: Migrate complex filters to custom workers where beneficial
5. **Phase 5**: Deprecate and remove transform function pointer

## Example Implementations

### Simple Low-Pass Filter (Using Stateful Map Worker)
```c
typedef struct {
    float alpha;
    float prev_value;
} LowPassState;

void lowpass_stateful_map(const float* in, float* out, size_t n, void* state_ptr) {
    LowPassState* state = (LowPassState*)state_ptr;
    for (size_t i = 0; i < n; i++) {
        state->prev_value = state->alpha * in[i] + 
                           (1 - state->alpha) * state->prev_value;
        out[i] = state->prev_value;
    }
}

// Create filter
Bp_Filter_t* create_lowpass_filter(float alpha) {
    BpFilterConfig config = BP_CONFIG_FLOAT_STANDARD;
    config.worker = BpStatefulMapWorker;
    
    Bp_Filter_t* filter = BpFilter_Create(&config);
    
    // Set up stateful context
    typedef struct {
        BpStatefulMapFunc map_func;
        void* state;
    } StatefulMapContext;
    
    LowPassState* lp_state = malloc(sizeof(LowPassState));
    lp_state->alpha = alpha;
    lp_state->prev_value = 0.0f;
    
    StatefulMapContext* ctx = malloc(sizeof(StatefulMapContext));
    ctx->map_func = lowpass_stateful_map;
    ctx->state = lp_state;
    
    filter->context = ctx;
    return filter;
}
```

### Complex Waveform Generator (Using Function Generator Worker)
```c
// Example: Chirp signal generator
typedef struct {
    float start_freq;
    float end_freq;
    float duration;
    float amplitude;
} ChirpParams;

void generate_chirp(void* output, size_t n_samples, double start_time, void* params_ptr) {
    float* out = (float*)output;
    ChirpParams* params = (ChirpParams*)params_ptr;
    
    double sample_rate = 48000.0;  // Could be passed in params
    double dt = 1.0 / sample_rate;
    
    for (size_t i = 0; i < n_samples; i++) {
        double t = start_time + i * dt;
        double progress = t / params->duration;
        
        if (progress <= 1.0) {
            // Linear frequency sweep
            double freq = params->start_freq + 
                         (params->end_freq - params->start_freq) * progress;
            out[i] = params->amplitude * sin(2.0 * M_PI * freq * t);
        } else {
            out[i] = 0.0f;  // Signal complete
        }
    }
}

// Create chirp generator
Bp_Filter_t* create_chirp_generator(float start_freq, float end_freq, 
                                   float duration, float amplitude) {
    BpFilterConfig config = BP_CONFIG_FLOAT_STANDARD;
    config.worker = BpFunctionGeneratorWorker;
    config.number_of_input_buffers = 0;  // Source filter
    
    Bp_Filter_t* filter = BpFilter_Create(&config);
    
    // Set up generator context
    typedef struct {
        BpGeneratorFunc gen_func;
        void* params;
        double current_time;
        double sample_rate;
    } GeneratorContext;
    
    ChirpParams* chirp = malloc(sizeof(ChirpParams));
    chirp->start_freq = start_freq;
    chirp->end_freq = end_freq;
    chirp->duration = duration;
    chirp->amplitude = amplitude;
    
    GeneratorContext* ctx = malloc(sizeof(GeneratorContext));
    ctx->gen_func = generate_chirp;
    ctx->params = chirp;
    ctx->current_time = 0.0;
    ctx->sample_rate = 48000.0;
    
    filter->context = ctx;
    return filter;
}
```

### Source Filter (Signal Generator)
```c
void* SignalGenWorker(void* filter_ptr) {
    Bp_Filter_t* f = (Bp_Filter_t*)filter_ptr;
    SignalGenState* state = f->context;
    
    while (f->running) {
        Bp_Batch_t out = Bp_allocate(f->sinks[0], 
                                     &f->sinks[0]->input_buffers[0]);
        
        float* data = (float*)out.data;
        for (size_t i = 0; i < out.capacity; i++) {
            data[i] = generate_sample(state);
        }
        
        out.head = out.capacity;
        Bp_submit_batch(f->sinks[0], 
                       &f->sinks[0]->input_buffers[0], &out);
    }
    
    return NULL;
}
```

### IIR Filter (Using Stateful Map Worker)
```c
// Biquad IIR filter with state preservation
typedef struct {
    // Filter coefficients
    float b0, b1, b2;  // Numerator
    float a1, a2;      // Denominator (a0 normalized to 1)
    
    // State variables
    float x1, x2;      // Previous inputs
    float y1, y2;      // Previous outputs
} BiquadState;

void biquad_process(const float* in, float* out, size_t n, void* state_ptr) {
    BiquadState* s = (BiquadState*)state_ptr;
    
    for (size_t i = 0; i < n; i++) {
        float x0 = in[i];
        
        // Direct Form I implementation
        float y0 = s->b0 * x0 + s->b1 * s->x1 + s->b2 * s->x2
                             - s->a1 * s->y1 - s->a2 * s->y2;
        
        out[i] = y0;
        
        // Update state
        s->x2 = s->x1;
        s->x1 = x0;
        s->y2 = s->y1;
        s->y1 = y0;
    }
}

// Create second-order low-pass filter
Bp_Filter_t* create_biquad_lowpass(float cutoff_freq, float sample_rate, float q) {
    BpFilterConfig config = BP_CONFIG_FLOAT_STANDARD;
    config.worker = BpStatefulMapWorker;
    
    Bp_Filter_t* filter = BpFilter_Create(&config);
    
    // Calculate coefficients
    float omega = 2.0f * M_PI * cutoff_freq / sample_rate;
    float sin_omega = sinf(omega);
    float cos_omega = cosf(omega);
    float alpha = sin_omega / (2.0f * q);
    
    BiquadState* state = calloc(1, sizeof(BiquadState));
    
    // Low-pass coefficients
    float a0 = 1.0f + alpha;
    state->b0 = (1.0f - cos_omega) / (2.0f * a0);
    state->b1 = (1.0f - cos_omega) / a0;
    state->b2 = state->b0;
    state->a1 = (-2.0f * cos_omega) / a0;
    state->a2 = (1.0f - alpha) / a0;
    
    // Set up stateful context
    typedef struct {
        BpStatefulMapFunc map_func;
        void* state;
    } StatefulMapContext;
    
    StatefulMapContext* ctx = malloc(sizeof(StatefulMapContext));
    ctx->map_func = biquad_process;
    ctx->state = state;
    
    filter->context = ctx;
    return filter;
}
```

### Multi-Input Synchronizer
```c
void* SyncWorker(void* filter_ptr) {
    Bp_Filter_t* f = (Bp_Filter_t*)filter_ptr;
    
    while (f->running) {
        // Wait for data on all inputs
        bool all_ready = wait_for_aligned_inputs(f);
        if (!all_ready) continue;
        
        // Process synchronized data
        process_aligned_batches(f);
        
        // Advance all inputs together
        advance_all_inputs(f);
    }
    
    return NULL;
}
```

## Conclusion

The worker-centric design provides a cleaner, more flexible architecture that scales from simple element-wise operations to complex multi-input synchronization scenarios. By allowing each filter to own its execution model, we eliminate the impedance mismatch between what filters need and what the framework provides, resulting in simpler code for both users and maintainers.

