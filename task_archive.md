## Core pass-through test cases
Implement a suite of Unity test cases on the Bp_Filter_t object instantiated with the following configurations:

- `transform` = `BpPassThroughTransform`.
- `ring_capacity_expo` = 4 (ring capacity of 16)
- `batch_capacity_expo` = 8 (capacity of 64)
- `overflow_behaviour` = BLOCKING (0)
- `dtype` = UNSIGNED

Test cases should include:
- Sawtooth:
    - Input Vector should: 
        - Increasing in steps of 1 from 0-255 and wrapping back arround to 0.
        - Total number of samples = 64*16*10
        - Sample period of 1ns
        - equal sample spacing.
        - Starting time stamp =0
    - Pass criterion:
        - Output vector is equal to input vector including sample timestamps, rate, magnitude etc.
        - Total processing time is less than 1ms.
- Partial batch
    - Input Vector:
        - As above but only 32 samples.
    - Pass criterion:
        - As above.

## Signal generator overview
Implement a child class of `Bp_Filter_t`  which can be used to generate periodic waveforms.

### periodic wave-forms:
- Square
- Sine
- Triangle
- Sawtooth

### The filter should have parameters for the following:
- frequency
- phase 
- amplitude
- x_offset.

### Testing
Implement a c unity test case that connects the output of the data-generator to a pass-through filter with a sawtooth waveform set, runs for 10ms and  checks the output.

## Arbitrary inputs/ouputs
Currently the framework only supports mapping a single source to single sink. The following changes will allow the framework to support mapping an arbitrary number of inputs to ouputs.

### Phase 1 - extract ring buffer parameters into component class.
`Bp_BatchBuffer_t` should consist of the following: 
- a pointer to the data buffer
- head & tail index
- empty and full conditions pthread conditions
- mutex for pthread

### Phase 2 - Define Input buffer array

### Phase 3 - Update `Bp_Worker` to handle multiple input buffers and multiple sinks.

### Phase 3 - Update join method.

## Agregator
Implement a child class of `Bp_Filter_t`  which can be used to accumulate input batches into a single contiguous vector up to some maximum size.

### Atributes
- Additional attributes should include:
- Max memory allocation.
- Starting time-stamp.

### Alocation behaviour
### Data structure
Data should be structured as an array of 

## Completion / Termination Behaviour

### Goal

It is desirable to have a way of allowing a source to signaling data completion to it's children. 

### Tasks

- Propose an aproach to implementing the desired behaviour.
- Identify any potential problems/side-effect implementing this behaviour may cause.
- Propose alternative workarounds to achieve this behaviour.

### Completion behaviour

- When a filter empties all of it's input buffers and the completion signal for each input buffer is set it should set the completion signal for it's own sinks and stop it's own processing thread.
- This should result in a a cascade of terminations down the pipeline.

### Termination behaviour

- If a source has an error and stops data-generation it should also set the completion signal on all of its sinks.
