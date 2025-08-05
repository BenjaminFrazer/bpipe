#include "properties.h"
#include <stdio.h>
#include <string.h>

/* Property metadata */
static const char* property_names[PROP_COUNT_MVP] = {
    [PROP_DATA_TYPE] = "data_type",
    [PROP_MIN_BATCH_CAPACITY] = "min_batch_capacity",
    [PROP_MAX_BATCH_CAPACITY] = "max_batch_capacity",
    [PROP_SAMPLE_RATE_HZ] = "sample_rate_hz"};

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

Bp_EC prop_set_sample_rate(PropertyTable_t* table, uint32_t rate_hz)
{
  if (!table) return Bp_EC_NULL_POINTER;
  table->properties[PROP_SAMPLE_RATE_HZ].known = true;
  table->properties[PROP_SAMPLE_RATE_HZ].value.u32 = rate_hz;
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

bool prop_get_sample_rate(const PropertyTable_t* table, uint32_t* rate_hz)
{
  if (!table || !rate_hz) return false;
  if (!table->properties[PROP_SAMPLE_RATE_HZ].known) return false;
  *rate_hz = table->properties[PROP_SAMPLE_RATE_HZ].value.u32;
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
          snprintf(error_msg, error_msg_size,
                   "Property '%s' is not present but must be >= %u",
                   property_names[constraint->property],
                   constraint->operand.u32);
        }
        return false;
      }
      if (prop->value.u32 < constraint->operand.u32) {
        if (error_msg) {
          snprintf(error_msg, error_msg_size,
                   "Property '%s' (%u) is less than required minimum (%u)",
                   property_names[constraint->property], prop->value.u32,
                   constraint->operand.u32);
        }
        return false;
      }
      break;

    case CONSTRAINT_OP_LTE:
      if (!prop->known) {
        if (error_msg) {
          snprintf(error_msg, error_msg_size,
                   "Property '%s' is not present but must be <= %u",
                   property_names[constraint->property],
                   constraint->operand.u32);
        }
        return false;
      }
      if (prop->value.u32 > constraint->operand.u32) {
        if (error_msg) {
          snprintf(error_msg, error_msg_size,
                   "Property '%s' (%u) is greater than required maximum (%u)",
                   property_names[constraint->property], prop->value.u32,
                   constraint->operand.u32);
        }
        return false;
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

  /* Check each constraint */
  for (size_t i = 0; i < downstream_contract->n_input_constraints; i++) {
    const InputConstraint_t* constraint =
        &downstream_contract->input_constraints[i];

    /* Validate property index */
    if (constraint->property >= PROP_COUNT_MVP) {
      return Bp_EC_INVALID_CONFIG;
    }

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

  /* Apply filter's output behaviors */
  for (size_t i = 0; i < filter_contract->n_output_behaviors; i++) {
    const OutputBehavior_t* behavior = &filter_contract->output_behaviors[i];

    /* Validate property index */
    if (behavior->property >= PROP_COUNT_MVP) {
      continue; /* Skip invalid properties */
    }

    apply_behavior(&downstream.properties[behavior->property], behavior);
  }

  return downstream;
}

/* Extract properties from a batch buffer configuration */
PropertyTable_t prop_from_buffer_config(const BatchBuffer_config* config)
{
  PropertyTable_t table = prop_table_init();

  if (!config) {
    return table;
  }

  /* Set data type property */
  prop_set_dtype(&table, config->dtype);

  /* Calculate batch capacity from exponent */
  uint32_t batch_capacity = 1U << config->batch_capacity_expo;
  prop_set_min_batch_capacity(&table, batch_capacity);
  prop_set_max_batch_capacity(&table, batch_capacity);

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
  if (prop >= PROP_COUNT_MVP) {
    return "unknown";
  }
  return property_names[prop];
}