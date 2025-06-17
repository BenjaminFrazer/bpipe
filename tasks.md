# Tasks

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

## Drop samples on overflow
Implement a behaviour whereby samples are dropped if the `sink` overflows controlled by the `overflow_behaviour` flag.

## 
