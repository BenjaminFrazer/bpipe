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

## OBJ_INIT

### Goals

Implement an initialisation function for `Bp_Filter_t`. This function should initialise the filter struct and leave it ready to use.

### Notes
- Where a filter is a sub-class of another filter it should call the parent class initialiser as well as doing it's own subsiquent initialisaiton.
- c Initialisation function should initialise pthread mutex and cond variables.
- Do not write tests for the python functionality.
- For component structs invoke these with existing initialisers.


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


## OVERFLOW_BEHAVIOUR
Implement a behaviour whereby samples are dropped if the `sink` overflows controlled by the `overflow_behaviour` flag.


## OBJ_INIT

### Goals
Implement an initialisation function for each of the existing types of filters.
Where a filter is a sub-class of another filter it should call the parent class initialiser as well as doing it's own subsiquent initialisaiton.

### Notes
- Object initialisation function should initialise pthread mutex and cond variables.

## BUFF_INIT

### Goal

For the data buffer object (`Bp_BatchBuffer_t`):
- Create an initialiser function which will produce a "ready-to-use" Batch buffer.
- Create a de-initialiser function clean up all allocated memory for a batch buffer.

## MULTI_INPUT_OUTPUT

### Goals
Expand the architecture of the filter class to support a multi-input/multi output architecture.

## START_STOP

### Goal
- Give the user the ability to start and stop filter threads from running.
- `start` should start the worker thread.
- `stop` should stop and join the worker thread.

## PYTHON_WRAPPERS
- Create Python Wrappers for the following Filters

### Goal
- Provide a high level python interface to allow the user to combine and compose arbitrary filters.

### Architecture
- The c layer should expose ony two core generic filter types:
    - Built-in :: Where the data transformation happens in in a C function.
    - Custom :: Where the user may provide a custom python transform function.

### Built-in filter type
- Should be implemented as a python class and should expose the following methods, each calling the relevant underlying c function:
    - `start`
    - `stop`
    - `__init__`
    - `__de_init__`
wrapping a customized c filter struct.
- This
- single highly configurable c based python filter should be exposed.
- decide whether it's better to select and configure filter from a factory class method or from __init__ function.
- This should take the form of a factory as oposed to a class instance.

### considerations
- create python stubs for auto-completion
- create a basic working python example for how to use the python API.
