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

## PYTEST

### Goal
- add pytest unit test framework to this repository.
- migrate existing python tests to pytest.

## SOURCE_SINK_FLAG

### Goal
- add `is_source` and `is_sink` flag to core filter type.
- allows certain filters to be only inputs or outputs.

### Behaviour
- if `is_source=0` the filter's `set_sink` should fail.
- if `is_sink=0` a filter attempting to call `set_sink` on this filter as a target should fail.

## PYFILT_AGREGATOR

### Goal
- create a C based filter which will agregate each input into a seperate numpy vector.
- the numpy array for each input buffer will be stored in a python list.
- the list of arrays will be accessible as the arrays member.
- each vector in arrays will be a read-only numpy vector of the type of it's corresponding input buffer.

### Use-case
- This filter will be a generic foundation for a family of python sinks such as:
    - Matplot lib plotters.
    - python based static (non-realtime) data analysis.
    - File writers.

### Behaviour
- This filter cannot be used as a source.
- each numpy vector will be dynamically re-sized as needed up to a max capacity.
- the filter will expose the `max_capacity` parameter in it's constructor as a kwarg. Default to a nice round power of 2 around 1Gb.


## SAWTOOTH_DEMO
- Create a demo script that chains a sawtooth data-source with a pass-through and writes to a plot aggregator.

### Goal
- a test to exersise all core features of the library working together.
- Demonstrates filter chaining & interfaces.
- low maintainance/high coverage
- representative of usage
- visual feedback

### Considerations/justification
- less maintainance burden
- sawtooth passthrough combo makes data corruption very obvious
- a back-to-bach test which tests three core filter components + python wrappers at "system level"

### behaviour
- The script should keep the plot window open untill the user kills it.
- keep python code concise simple and readable avoid try catches. 

## UPDATE_PYTESTS

### Goal
- refactor, simplify & integrate python unit tests.
- bing inline with style guide
- update to use pytest
- prune obsolete tests

## PLOT_SINK

### Goal
- build a pure python matplotlib plotting sink on-top of the agregator class.

### Behaviour
- filter should expose a `plot()` method which will create a full matplotlib time-domain plot each enabled input buffer.
- all traces should be on the same axes.
- plot can optionally take an existing figure handle for integration with gui's but will create a new window if not provided.

## ABSTRACTED_BATCH_ACESSORS
- I would preffer to abstract the filter interface to remove buffer pointers so the  API simply reffering to the index of the input i.e. input 0, 1, 2 etc.

### Goal
- simplify the API by hiding implementation details
- seperate concerns 

### Example
Original
```c
Bp_submit_batch(Bp_Filter_t* dpipe, Bp_BatchBuffer_t* buf)
```

New:
```c
Bp_submit_batch(Bp_Filter_t* dpipe, int buff_idx)
```

### affected API's
- `Bp_submit_batch`
- `Bp_delete_tail`
- `Bp_head`
- perhaps others
