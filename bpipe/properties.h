#ifndef BPIPE_PROPERTIES_H
#define BPIPE_PROPERTIES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "batch_buffer.h"
#include "bperr.h"

/* Core Properties for MVP - focusing on most critical compatibility issues */
typedef enum {
  /* Data type property */
  PROP_DATA_TYPE, /* SampleDtype_t value */

  /* Batch capacity properties (in samples, not exponents) */
  PROP_MIN_BATCH_CAPACITY, /* Minimum accepted batch size in samples */
  PROP_MAX_BATCH_CAPACITY, /* Maximum accepted batch size in samples */

  /* Timing property */
  PROP_SAMPLE_RATE_HZ, /* Fixed sample rate in Hz (0 = variable/unknown) */

  PROP_COUNT_MVP /* Just 4 properties for MVP */
} SignalProperty_t;

/* Constraint operators for input validation */
typedef enum {
  CONSTRAINT_OP_EXISTS, /* Property must be present */
  CONSTRAINT_OP_EQ,     /* Property must equal value */
  CONSTRAINT_OP_GTE,    /* Property must be >= value (for capacities) */
  CONSTRAINT_OP_LTE,    /* Property must be <= value (for capacities) */
} ConstraintOp_t;

/* Behavior operators for output transformation */
typedef enum {
  BEHAVIOR_OP_SET,      /* Set property to fixed value */
  BEHAVIOR_OP_PRESERVE, /* Pass through unchanged (default) */
} BehaviorOp_t;

/* Simple property storage */
typedef struct {
  bool known;
  union {
    SampleDtype_t dtype;
    uint32_t u32;
  } value;
} Property_t;

/* Property table containing all properties */
typedef struct {
  Property_t properties[PROP_COUNT_MVP];
} PropertyTable_t;

/* Input constraint structure */
typedef struct {
  SignalProperty_t property;
  ConstraintOp_t op;
  union {
    SampleDtype_t dtype;
    uint32_t u32;
  } operand;
} InputConstraint_t;

/* Output behavior structure */
typedef struct {
  SignalProperty_t property;
  BehaviorOp_t op;
  union {
    SampleDtype_t dtype;
    uint32_t u32;
  } operand;
} OutputBehavior_t;

/* Filter contract defining requirements and behaviors */
typedef struct {
  const InputConstraint_t* input_constraints;
  size_t n_input_constraints;
  const OutputBehavior_t* output_behaviors;
  size_t n_output_behaviors;
} FilterContract_t;

/* Property system API */

/* Initialize a property table with default values */
PropertyTable_t prop_table_init(void);

/* Set a property in the table */
Bp_EC prop_set_dtype(PropertyTable_t* table, SampleDtype_t dtype);
Bp_EC prop_set_min_batch_capacity(PropertyTable_t* table, uint32_t capacity);
Bp_EC prop_set_max_batch_capacity(PropertyTable_t* table, uint32_t capacity);
Bp_EC prop_set_sample_rate(PropertyTable_t* table, uint32_t rate_hz);

/* Get a property from the table (returns false if unknown) */
bool prop_get_dtype(const PropertyTable_t* table, SampleDtype_t* dtype);
bool prop_get_min_batch_capacity(const PropertyTable_t* table,
                                 uint32_t* capacity);
bool prop_get_max_batch_capacity(const PropertyTable_t* table,
                                 uint32_t* capacity);
bool prop_get_sample_rate(const PropertyTable_t* table, uint32_t* rate_hz);

/* Validate that upstream properties meet downstream constraints */
Bp_EC prop_validate_connection(const PropertyTable_t* upstream_props,
                               const FilterContract_t* downstream_contract,
                               char* error_msg, size_t error_msg_size);

/* Propagate properties through a filter (inheritance + behaviors) */
PropertyTable_t prop_propagate(const PropertyTable_t* upstream,
                               const FilterContract_t* filter_contract);

/* Extract properties from a batch buffer configuration */
PropertyTable_t prop_from_buffer_config(const BatchBuffer_config* config);

/* Debug/logging utilities */
void prop_describe_table(const PropertyTable_t* table, char* buffer,
                         size_t size);
const char* prop_get_name(SignalProperty_t prop);

#endif /* BPIPE_PROPERTIES_H */