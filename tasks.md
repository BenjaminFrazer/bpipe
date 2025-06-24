# Tasks
- Goal
- Considerations
- Tests

- Task specification blurb.
    - Propose implementation to achieve goal: X
    - Identify Challenges and Impacts of changes.
    - Propose alternative aproaches to achieve goal.

## NOISE_SOURCE - Noise source
Implement a child class of `Bp_Filter_t`  which can be used to generate noise with configurable characteristics.
- Standard deviation.
- Distribution 0=gausian.
- Cumulative density. Integral of the distribution.

## OBJ_INIT

### Goals

Implement an initialisation function for each `c` filter variant. This function should initialise the filter struct and leave it ready to use.

### Notes
- Where a filter is a sub-class of another filter it should call the parent class initialiser as well as doing it's own subsiquent initialisaiton.
- c Initialisation function should initialise pthread mutex and cond variables.
- Do not write tests for the python functionality.

## PYTHON_WRAPPERS - Create Python Wrappers for the following Filters


## C_TEST_HARNESS - Propose Test framework for Pure C based

### Goals
Create test harness for filter components which achieves the following goals:
- Clear centrelized test configuration info.
- Low maintainance. - Ideally ag

## OVERFLOW_BEHAVIOUR - Drop samples on overflow
Implement a behaviour whereby samples are dropped if the `sink` overflows controlled by the `overflow_behaviour` flag.




