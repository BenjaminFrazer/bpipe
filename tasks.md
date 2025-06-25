# Tasks
- Goal
- Considerations
- Tests

- Task specification blurb.
    - Propose implementation to achieve goal: X
    - Identify Challenges and Impacts of changes.
    - Propose alternative aproaches to achieve goal.

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
- , highly configurable c based python wrapper should be exposed.
- This should take the form of a factory as oposed to a class instance.
- 


## C_TEST_HARNESS 
- Propose Test framework for Pure C based filters.

### Goals
Create test harness for filter components which achieves the following goals:
- Clear centrelized test configuration info.
- Low maintainance. - Ideally ag

## OVERFLOW_BEHAVIOUR
Implement a behaviour whereby samples are dropped if the `sink` overflows controlled by the `overflow_behaviour` flag.


## NOISE_SOURCE - Noise source
Implement a child class of `Bp_Filter_t`  which can be used to generate noise with configurable characteristics.
- Standard deviation.
- Distribution 0=gausian.
- Cumulative density. Integral of the distribution.

