#include "properties.h"
#include <stdio.h>
#include <string.h>
#include "core.h"

/* Property metadata - use explicit enum indexing */
static const char* property_names[PROP_COUNT_MVP + 1] = {
    [PROP_SLOT_AVAILABLE] = "slot_available",
    [PROP_DATA_TYPE] = "data_type",
    [PROP_MIN_BATCH_CAPACITY] = "min_batch_capacity",
    [PROP_MAX_BATCH_CAPACITY] = "max_batch_capacity",
    [PROP_SAMPLE_PERIOD_NS] = "sample_period_ns"};

/* Initialize a property table with default values */
PropertyTable_t prop_table_init(void)
{
  PropertyTable_t table;
  memset(&table, 0, sizeof(table));
  return table;
}

/* Set a property in the table */
Bp_EC prop_set_dtype(PropertyTable_t* table, SampleDtype_t dtype)
{
  if (!table) return Bp_EC_NULL_POINTER;
  table->properties[PROP_DATA_TYPE].known = true;
  table->properties[PROP_DATA_TYPE].value.dtype = dtype;
  return Bp_EC_OK;
}

Bp_EC prop_set_min_batch_capacity(PropertyTable_t* table, uint32_t capacity)
{
  if (!table) return Bp_EC_NULL_POINTER;
  table->properties[PROP_MIN_BATCH_CAPACITY].known = true;
  table->properties[PROP_MIN_BATCH_CAPACITY].value.u32 = capacity;
  return Bp_EC_OK;
}

Bp_EC prop_set_max_batch_capacity(PropertyTable_t* table, uint32_t capacity)
{
  if (!table) return Bp_EC_NULL_POINTER;
  table->properties[PROP_MAX_BATCH_CAPACITY].known = true;
  table->properties[PROP_MAX_BATCH_CAPACITY].value.u32 = capacity;
  return Bp_EC_OK;
}

Bp_EC prop_set_sample_period(PropertyTable_t* table, uint64_t period_ns)
{
  if (!table) return Bp_EC_NULL_POINTER;
  table->properties[PROP_SAMPLE_PERIOD_NS].known = true;
  table->properties[PROP_SAMPLE_PERIOD_NS].value.u64 = period_ns;
  return Bp_EC_OK;
}

/* Get a property from the table (returns false if unknown) */
bool prop_get_dtype(const PropertyTable_t* table, SampleDtype_t* dtype)
{
  if (!table || !dtype) return false;
  if (!table->properties[PROP_DATA_TYPE].known) return false;
  *dtype = table->properties[PROP_DATA_TYPE].value.dtype;
  return true;
}

bool prop_get_min_batch_capacity(const PropertyTable_t* table,
                                 uint32_t* capacity)
{
  if (!table || !capacity) return false;
  if (!table->properties[PROP_MIN_BATCH_CAPACITY].known) return false;
  *capacity = table->properties[PROP_MIN_BATCH_CAPACITY].value.u32;
  return true;
}

bool prop_get_max_batch_capacity(const PropertyTable_t* table,
                                 uint32_t* capacity)
{
  if (!table || !capacity) return false;
  if (!table->properties[PROP_MAX_BATCH_CAPACITY].known) return false;
  *capacity = table->properties[PROP_MAX_BATCH_CAPACITY].value.u32;
  return true;
}

bool prop_get_sample_period(const PropertyTable_t* table, uint64_t* period_ns)
{
  if (!table || !period_ns) return false;
  if (!table->properties[PROP_SAMPLE_PERIOD_NS].known) return false;
  *period_ns = table->properties[PROP_SAMPLE_PERIOD_NS].value.u64;
  return true;
}

/* Validate a single constraint */
static bool validate_constraint(const Property_t* prop,
                                const InputConstraint_t* constraint,
                                char* error_msg, size_t error_msg_size)
{
  switch (constraint->op) {
    case CONSTRAINT_OP_EXISTS:
      if (!prop->known) {
        if (error_msg) {
          snprintf(error_msg, error_msg_size,
                   "Required property '%s' is not present",
                   property_names[constraint->property]);
        }
        return false;
      }
      break;

    case CONSTRAINT_OP_EQ:
      if (!prop->known) {
        if (error_msg) {
          snprintf(error_msg, error_msg_size,
                   "Property '%s' is not present but must equal specific value",
                   property_names[constraint->property]);
        }
        return false;
      }
      /* Compare based on property type */
      if (constraint->property == PROP_DATA_TYPE) {
        if (prop->value.dtype != constraint->operand.dtype) {
          if (error_msg) {
            snprintf(error_msg, error_msg_size,
                     "Data type mismatch: expected %d, got %d",
                     constraint->operand.dtype, prop->value.dtype);
          }
          return false;
        }
      } else if (constraint->property == PROP_SAMPLE_PERIOD_NS) {
        if (prop->value.u64 != constraint->operand.u64) {
          if (error_msg) {
            snprintf(error_msg, error_msg_size,
                     "Property '%s' mismatch: expected %llu, got %llu",
                     property_names[constraint->property],
                     (unsigned long long) constraint->operand.u64,
                     (unsigned long long) prop->value.u64);
          }
          return false;
        }
      } else {
        if (prop->value.u32 != constraint->operand.u32) {
          if (error_msg) {
            snprintf(error_msg, error_msg_size,
                     "Property '%s' mismatch: expected %u, got %u",
                     property_names[constraint->property],
                     constraint->operand.u32, prop->value.u32);
          }
          return false;
        }
      }
      break;

    case CONSTRAINT_OP_GTE:
      if (!prop->known) {
        if (error_msg) {
          if (constraint->property == PROP_SAMPLE_PERIOD_NS) {
            snprintf(error_msg, error_msg_size,
                     "Property '%s' is not present but must be >= %llu",
                     property_names[constraint->property],
                     (unsigned long long) constraint->operand.u64);
          } else {
            snprintf(error_msg, error_msg_size,
                     "Property '%s' is not present but must be >= %u",
                     property_names[constraint->property],
                     constraint->operand.u32);
          }
        }
        return false;
      }
      if (constraint->property == PROP_SAMPLE_PERIOD_NS) {
        if (prop->value.u64 < constraint->operand.u64) {
          if (error_msg) {
            snprintf(
                error_msg, error_msg_size,
                "Property '%s' (%llu) is less than required minimum (%llu)",
                property_names[constraint->property],
                (unsigned long long) prop->value.u64,
                (unsigned long long) constraint->operand.u64);
          }
          return false;
        }
      } else {
        if (prop->value.u32 < constraint->operand.u32) {
          if (error_msg) {
            snprintf(error_msg, error_msg_size,
                     "Property '%s' (%u) is less than required minimum (%u)",
                     property_names[constraint->property], prop->value.u32,
                     constraint->operand.u32);
          }
          return false;
        }
      }
      break;

    case CONSTRAINT_OP_LTE:
      if (!prop->known) {
        if (error_msg) {
          if (constraint->property == PROP_SAMPLE_PERIOD_NS) {
            snprintf(error_msg, error_msg_size,
                     "Property '%s' is not present but must be <= %llu",
                     property_names[constraint->property],
                     (unsigned long long) constraint->operand.u64);
          } else {
            snprintf(error_msg, error_msg_size,
                     "Property '%s' is not present but must be <= %u",
                     property_names[constraint->property],
                     constraint->operand.u32);
          }
        }
        return false;
      }
      if (constraint->property == PROP_SAMPLE_PERIOD_NS) {
        if (prop->value.u64 > constraint->operand.u64) {
          if (error_msg) {
            snprintf(
                error_msg, error_msg_size,
                "Property '%s' (%llu) is greater than required maximum (%llu)",
                property_names[constraint->property],
                (unsigned long long) prop->value.u64,
                (unsigned long long) constraint->operand.u64);
          }
          return false;
        }
      } else {
        if (prop->value.u32 > constraint->operand.u32) {
          if (error_msg) {
            snprintf(error_msg, error_msg_size,
                     "Property '%s' (%u) is greater than required maximum (%u)",
                     property_names[constraint->property], prop->value.u32,
                     constraint->operand.u32);
          }
          return false;
        }
      }
      break;
  }

  return true;
}

/* Validate that upstream properties meet downstream constraints */
Bp_EC prop_validate_connection(const PropertyTable_t* upstream_props,
                               const FilterContract_t* downstream_contract,
                               char* error_msg, size_t error_msg_size)
{
  if (!upstream_props || !downstream_contract) {
    return Bp_EC_NULL_POINTER;
  }

  /* Clear error message if provided */
  if (error_msg && error_msg_size > 0) {
    error_msg[0] = '\0';
  }

  /* Check each constraint using count */
  const InputConstraint_t* constraints = downstream_contract->input_constraints;
  for (size_t i = 0; i < downstream_contract->n_input_constraints; i++) {
    const InputConstraint_t* constraint = &constraints[i];

    /* Validate property index */
    if (constraint->property > PROP_COUNT_MVP || constraint->property < 1) {
      return Bp_EC_INVALID_CONFIG;
    }

    /* Direct indexing - enums match array indices */
    const Property_t* prop = &upstream_props->properties[constraint->property];

    if (!validate_constraint(prop, constraint, error_msg, error_msg_size)) {
      return Bp_EC_PROPERTY_MISMATCH;
    }
  }

  /* All constraints satisfied */
  return Bp_EC_OK;
}

/* Apply a single behavior */
static void apply_behavior(Property_t* prop, const OutputBehavior_t* behavior)
{
  switch (behavior->op) {
    case BEHAVIOR_OP_SET:
      prop->known = true;
      if (behavior->property == PROP_DATA_TYPE) {
        prop->value.dtype = behavior->operand.dtype;
      } else {
        prop->value.u32 = behavior->operand.u32;
      }
      break;

    case BEHAVIOR_OP_PRESERVE:
      /* Property already inherited from upstream - do nothing */
      break;
  }
}

/* Propagate properties through a filter (inheritance + behaviors) */
PropertyTable_t prop_propagate(const PropertyTable_t* upstream,
                               const FilterContract_t* filter_contract)
{
  /* Start with upstream properties (inheritance by default) */
  PropertyTable_t downstream = *upstream;

  if (!filter_contract) {
    return downstream;
  }

  /* Apply filter's output behaviors using count */
  const OutputBehavior_t* behaviors = filter_contract->output_behaviors;
  if (behaviors) {
    for (size_t i = 0; i < filter_contract->n_output_behaviors; i++) {
      const OutputBehavior_t* behavior = &behaviors[i];

      /* Validate property index */
      if (behavior->property > PROP_COUNT_MVP || behavior->property < 1) {
        continue; /* Skip invalid properties */
      }

      /* Direct indexing - enums match array indices */
      apply_behavior(&downstream.properties[behavior->property], behavior);
    }
  }

  return downstream;
}

/* Utility function to extract batch capacity from buffer config */
static inline uint32_t prop_batch_cap_from_buff_config(
    const BatchBuffer_config* config)
{
  return config ? (1U << config->batch_capacity_expo) : 0;
}

/* Utility function to extract data type from buffer config */
static inline SampleDtype_t prop_dtype_from_buff_config(
    const BatchBuffer_config* config)
{
  return config ? config->dtype : DTYPE_NDEF;
}

/* Extract properties from a batch buffer configuration */
PropertyTable_t prop_from_buffer_config(const BatchBuffer_config* config)
{
  PropertyTable_t table = prop_table_init();

  if (!config) {
    return table;
  }

  /* Set data type property using utility function */
  SampleDtype_t dtype = prop_dtype_from_buff_config(config);
  if (dtype != DTYPE_NDEF) {
    prop_set_dtype(&table, dtype);
  }

  /* Set batch capacity properties - default to exact capacity match
   * Filters that support partial batches should override these after init */
  uint32_t batch_capacity = prop_batch_cap_from_buff_config(config);
  if (batch_capacity > 0) {
    prop_set_min_batch_capacity(&table, batch_capacity);
    prop_set_max_batch_capacity(&table, batch_capacity);
  }

  /* Note: Sample rate is not available in buffer config,
   * must be set separately if known */

  return table;
}

/* Debug/logging utilities */
void prop_describe_table(const PropertyTable_t* table, char* buffer,
                         size_t size)
{
  if (!table || !buffer || size == 0) return;

  size_t written = 0;
  written += snprintf(buffer + written, size - written, "Property Table:\n");

  for (int i = 0; i < PROP_COUNT_MVP && written < size; i++) {
    const Property_t* prop = &table->properties[i];
    if (prop->known) {
      written += snprintf(buffer + written, size - written,
                          "  %s: ", property_names[i]);

      switch (i) {
        case PROP_DATA_TYPE:
          written += snprintf(buffer + written, size - written, "%d\n",
                              prop->value.dtype);
          break;
        default:
          written += snprintf(buffer + written, size - written, "%u\n",
                              prop->value.u32);
          break;
      }
    }
  }
}

const char* prop_get_name(SignalProperty_t prop)
{
  if (prop < 0 || prop >= PROP_COUNT_MVP) return "unknown";
  return property_names[prop] ? property_names[prop] : "unknown";
}

/* Append a constraint to filter's input constraints array */
bool prop_append_constraint(Filter_t* filter, SignalProperty_t prop,
                            ConstraintOp_t op, const void* operand)
{
  if (!filter || !operand) return false;

  /* Check if array is full */
  if (filter->n_input_constraints >= MAX_CONSTRAINTS) {
    return false;
  }

  /* Add constraint at next available position */
  InputConstraint_t* constraint =
      &filter->input_constraints[filter->n_input_constraints];
  constraint->property = prop;
  constraint->op = op;

  /* Set operand based on property type */
  if (prop == PROP_DATA_TYPE) {
    constraint->operand.dtype = *(const SampleDtype_t*) operand;
  } else if (prop == PROP_SAMPLE_PERIOD_NS) {
    constraint->operand.u64 = *(const uint64_t*) operand;
  } else {
    constraint->operand.u32 = *(const uint32_t*) operand;
  }

  /* Increment count */
  filter->n_input_constraints++;

  /* Update contract count */
  filter->contract.n_input_constraints = filter->n_input_constraints;

  return true;
}

/* Append a behavior to filter's output behaviors array */
bool prop_append_behavior(Filter_t* filter, SignalProperty_t prop,
                          BehaviorOp_t op, const void* operand)
{
  if (!filter || !operand) return false;

  /* Check if array is full */
  if (filter->n_output_behaviors >= MAX_BEHAVIORS) {
    return false;
  }

  /* Add behavior at next available position */
  OutputBehavior_t* behavior =
      &filter->output_behaviors[filter->n_output_behaviors];
  behavior->property = prop;
  behavior->op = op;

  /* Set operand based on property type */
  if (prop == PROP_DATA_TYPE) {
    behavior->operand.dtype = *(const SampleDtype_t*) operand;
  } else if (prop == PROP_SAMPLE_PERIOD_NS) {
    behavior->operand.u64 = *(const uint64_t*) operand;
  } else {
    behavior->operand.u32 = *(const uint32_t*) operand;
  }

  /* Increment count */
  filter->n_output_behaviors++;

  /* Update contract count */
  filter->contract.n_output_behaviors = filter->n_output_behaviors;

  return true;
}

/* Generate input constraints from buffer configuration */
void prop_constraints_from_buffer_append(Filter_t* filter,
                                         const BatchBuffer_config* config,
                                         bool accepts_partial_fill)
{
  if (!filter || !config) return;

  /* Add data type constraint */
  prop_append_constraint(filter, PROP_DATA_TYPE, CONSTRAINT_OP_EQ,
                         &config->dtype);

  uint32_t capacity = 1U << config->batch_capacity_expo;

  if (accepts_partial_fill) {
    /* Can handle any size from 1 to capacity */
    uint32_t min = 1;
    prop_append_constraint(filter, PROP_MIN_BATCH_CAPACITY, CONSTRAINT_OP_GTE,
                           &min);
    prop_append_constraint(filter, PROP_MAX_BATCH_CAPACITY, CONSTRAINT_OP_LTE,
                           &capacity);
  } else {
    /* Requires exact capacity */
    prop_append_constraint(filter, PROP_MIN_BATCH_CAPACITY, CONSTRAINT_OP_EQ,
                           &capacity);
    prop_append_constraint(filter, PROP_MAX_BATCH_CAPACITY, CONSTRAINT_OP_EQ,
                           &capacity);
  }
}

/* Helper function for buffer-based filters to set output behaviors */
void prop_set_output_behavior_for_buffer_filter(
    Filter_t* filter, const BatchBuffer_config* config, bool adapt_batch_size,
    bool guarantee_full)
{
  if (!filter || !config) return;

  /* Set data type behavior - always preserve */
  prop_append_behavior(filter, PROP_DATA_TYPE, BEHAVIOR_OP_PRESERVE, NULL);

  /* Set sample period behavior - always preserve */
  prop_append_behavior(filter, PROP_SAMPLE_PERIOD_NS, BEHAVIOR_OP_PRESERVE,
                       NULL);

  uint32_t capacity = 1U << config->batch_capacity_expo;

  if (adapt_batch_size) {
    /* Filter will determine output sizes based on downstream requirements */
    /* Don't set any batch size behaviors - will be set later when detected */
  } else {
    /* Passthrough mode - output sizes match input (clamped to buffer capacity)
     */
    if (guarantee_full) {
      /* Always output full batches */
      prop_append_behavior(filter, PROP_MIN_BATCH_CAPACITY, BEHAVIOR_OP_SET,
                           &capacity);
      prop_append_behavior(filter, PROP_MAX_BATCH_CAPACITY, BEHAVIOR_OP_SET,
                           &capacity);
    } else {
      /* Output sizes can vary - preserve input behavior */
      prop_append_behavior(filter, PROP_MIN_BATCH_CAPACITY,
                           BEHAVIOR_OP_PRESERVE, NULL);
      prop_append_behavior(filter, PROP_MAX_BATCH_CAPACITY,
                           BEHAVIOR_OP_PRESERVE, NULL);
    }
  }
}