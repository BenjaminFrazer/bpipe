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
  PROP_SAMPLE_PERIOD_NS, /* Sample period in nanoseconds (0 = variable/unknown)
                          */

  PROP_COUNT_MVP /* Count of actual properties */
} SignalProperty_t;

/* Constraint operators for input validation */
typedef enum {
  CONSTRAINT_OP_EXISTS, /* Property must be present */
  CONSTRAINT_OP_EQ,     /* Property must equal value */
  CONSTRAINT_OP_GTE,    /* Property must be >= value (for capacities) */
  CONSTRAINT_OP_LTE,    /* Property must be <= value (for capacities) */
  CONSTRAINT_OP_MULTI_INPUT_ALIGNED /* Property must match across all inputs in
                                       mask */
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
    uint64_t u64;
  } value;
} Property_t;

/* Property table containing all properties
 * Array is sized to handle enum values starting at 1 */
typedef struct {
  Property_t
      properties[PROP_COUNT_MVP + 1];  // +1 for PROP_SLOT_AVAILABLE at index 0
} PropertyTable_t;

/* Port mask definitions for targeting specific inputs/outputs */
#define INPUT_0 0x00000001 /* Port 0 only */
#define INPUT_1 0x00000002 /* Port 1 only */
#define INPUT_2 0x00000004 /* Port 2 only */
#define INPUT_3 0x00000008 /* Port 3 only */
#define INPUT_ALL \
  0xFFFFFFFF /* All input ports (default for backward compatibility) */

#define OUTPUT_0 0x00000001 /* Port 0 only */
#define OUTPUT_1 0x00000002 /* Port 1 only */
#define OUTPUT_2 0x00000004 /* Port 2 only */
#define OUTPUT_3 0x00000008 /* Port 3 only */
#define OUTPUT_ALL \
  0xFFFFFFFF /* All output ports (default for backward compatibility) */

/* Input constraint structure */
typedef struct {
  SignalProperty_t property;
  ConstraintOp_t op;
  uint32_t input_mask; /* Bitmask indicating which input ports this constraint
                          applies to */
  union {
    SampleDtype_t dtype;
    uint32_t u32;
    uint64_t u64;
  } operand;
} InputConstraint_t;

/* Output behavior structure */
typedef struct {
  SignalProperty_t property;
  BehaviorOp_t op;
  uint32_t output_mask; /* Bitmask indicating which output ports this behavior
                           applies to */
  union {
    SampleDtype_t dtype;
    uint32_t u32;
    uint64_t u64;
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
Bp_EC prop_set_sample_period(PropertyTable_t* table, uint64_t period_ns);

/* Get a property from the table (returns false if unknown) */
bool prop_get_dtype(const PropertyTable_t* table, SampleDtype_t* dtype);
bool prop_get_min_batch_capacity(const PropertyTable_t* table,
                                 uint32_t* capacity);
bool prop_get_max_batch_capacity(const PropertyTable_t* table,
                                 uint32_t* capacity);
bool prop_get_sample_period(const PropertyTable_t* table, uint64_t* period_ns);

/* Validate that upstream properties meet downstream constraints
 * @param input_port: The specific input port being connected (0-based index)
 */
Bp_EC prop_validate_connection(const PropertyTable_t* upstream_props,
                               const FilterContract_t* downstream_contract,
                               uint32_t input_port, char* error_msg,
                               size_t error_msg_size);

/* Validate multi-input alignment constraints for a specific connection
 * This checks if the new connection's properties align with already-connected
 * inputs
 * @param sink: The filter receiving the connection
 * @param new_input_port: The input port being connected (0-based index)
 * @param new_input_props: Properties of the upstream filter being connected
 */
Bp_EC prop_validate_multi_input_alignment(
    const struct _Filter_t* sink, uint32_t new_input_port,
    const PropertyTable_t* new_input_props, char* error_msg,
    size_t error_msg_size);

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
 * @param input_mask: Bitmask indicating which input ports this constraint
 * applies to
 */
bool prop_append_constraint(struct _Filter_t* filter, SignalProperty_t prop,
                            ConstraintOp_t op, const void* operand,
                            uint32_t input_mask);

/* Append a behavior to a filter's output behaviors array
 * Returns true if successful, false if array is full
 * The array must have been initialized with PROP_SENTINEL at the end
 * @param output_mask: Bitmask indicating which output ports this behavior
 * applies to
 */
bool prop_append_behavior(struct _Filter_t* filter, SignalProperty_t prop,
                          BehaviorOp_t op, const void* operand,
                          uint32_t output_mask);

/* Generate input constraints from buffer configuration
 * Appends constraints to the filter's input_constraints array
 * @param accepts_partial_fill: If true, accepts batches with head < capacity
 */
void prop_constraints_from_buffer_append(struct _Filter_t* filter,
                                         const BatchBuffer_config* config,
                                         bool accepts_partial_fill);

/* Helper function for buffer-based filters to set output behaviors
 * Handles both input constraints and output behaviors for common filter
 * patterns
 * @param filter: The filter to configure
 * @param config: Buffer configuration for the filter
 * @param adapt_batch_size: false = passthrough input sizes (clamped to buffer),
 *                          true = filter determines output size
 * @param guarantee_full: false = allows partial batches,
 *                       true = always outputs full batches (min==max)
 */
void prop_set_output_behavior_for_buffer_filter(
    struct _Filter_t* filter, const BatchBuffer_config* config,
    bool adapt_batch_size, bool guarantee_full);

/* Debug/logging utilities */
void prop_describe_table(const PropertyTable_t* table, char* buffer,
                         size_t size);
const char* prop_get_name(SignalProperty_t prop);

/* Conversion helpers for sample rate <-> period */
static inline uint64_t sample_rate_to_period_ns(uint32_t rate_hz)
{
  if (rate_hz == 0) return 0;  // 0 means variable/undefined
  return 1000000000ULL / rate_hz;
}

static inline uint32_t period_ns_to_sample_rate(uint64_t period_ns)
{
  if (period_ns == 0) return 0;  // 0 means variable/undefined
  return (uint32_t) (1000000000ULL / period_ns);
}

/* Convenience functions for setting/getting sample rate in Hz */
static inline Bp_EC prop_set_sample_rate_hz(PropertyTable_t* table,
                                            uint32_t rate_hz)
{
  uint64_t period_ns = sample_rate_to_period_ns(rate_hz);
  return prop_set_sample_period(table, period_ns);
}

static inline bool prop_get_sample_rate_hz(const PropertyTable_t* table,
                                           uint32_t* rate_hz)
{
  uint64_t period_ns;
  if (!prop_get_sample_period(table, &period_ns)) return false;
  *rate_hz = period_ns_to_sample_rate(period_ns);
  return true;
}

#endif /* BPIPE_PROPERTIES_H */