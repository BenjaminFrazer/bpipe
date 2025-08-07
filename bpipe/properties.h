#ifndef BPIPE_PROPERTIES_H
#define BPIPE_PROPERTIES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "batch_buffer.h"
#include "bperr.h"

/* Forward declaration */
struct _Filter_t;

/* Core Properties for MVP - focusing on most critical compatibility issues */
typedef enum {
  /* Special values for array management */
  PROP_SLOT_AVAILABLE = 0, /* Empty slot that can be filled */

  /* Data type property */
  PROP_DATA_TYPE = 1, /* SampleDtype_t value */

  /* Batch capacity properties (in samples, not exponents) */
  PROP_MIN_BATCH_CAPACITY, /* Minimum batch size emitted by filter */
  PROP_MAX_BATCH_CAPACITY, /* Maximum batch size emitted by filter */

  /* Timing property */
  PROP_SAMPLE_RATE_HZ, /* Fixed sample rate in Hz (0 = variable/unknown) */

  PROP_COUNT_MVP /* Count of actual properties */
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

/* Property table containing all properties
 * Array is sized to handle enum values starting at 1 */
typedef struct {
  Property_t
      properties[PROP_COUNT_MVP + 1];  // +1 for PROP_SLOT_AVAILABLE at index 0
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

/* Extract properties from a batch buffer configuration
 * Sets both min and max batch capacity to the buffer's capacity
 * Data type is set from the buffer config
 * Sample rate must be set separately if known
 */
PropertyTable_t prop_from_buffer_config(const BatchBuffer_config* config);

/* Append a constraint to a filter's input constraints array
 * Returns true if successful, false if array is full
 * The array must have been initialized with PROP_SENTINEL at the end
 */
bool prop_append_constraint(struct _Filter_t* filter, SignalProperty_t prop,
                            ConstraintOp_t op, const void* operand);

/* Append a behavior to a filter's output behaviors array
 * Returns true if successful, false if array is full
 * The array must have been initialized with PROP_SENTINEL at the end
 */
bool prop_append_behavior(struct _Filter_t* filter, SignalProperty_t prop,
                          BehaviorOp_t op, const void* operand);

/* Generate input constraints from buffer configuration
 * Appends constraints to the filter's input_constraints array
 * @param accepts_partial_fill: If true, accepts batches with head < capacity
 */
void prop_constraints_from_buffer_append(struct _Filter_t* filter,
                                         const BatchBuffer_config* config,
                                         bool accepts_partial_fill);

/* Debug/logging utilities */
void prop_describe_table(const PropertyTable_t* table, char* buffer,
                         size_t size);
const char* prop_get_name(SignalProperty_t prop);

#endif /* BPIPE_PROPERTIES_H */