#ifndef BPIPE_PIPELINE_H
#define BPIPE_PIPELINE_H

#include "core.h"

/* Connection specification (direct pointer references) */
typedef struct {
  Filter_t* from_filter; /* Source filter pointer */
  size_t from_port;      /* Source port */
  Filter_t* to_filter;   /* Destination filter pointer  */
  size_t to_port;        /* Destination port */
} Connection_t;

/* Configuration structure (follows bpipe2 patterns) */
typedef struct _Pipeline_config_t {
  const char* name;
  BatchBuffer_config buff_config; /* For pipeline's input buffer */
  long timeout_us;

  /* Filter topology (direct references) */
  Filter_t** filters; /* Array of pre-initialized filter pointers */
  size_t n_filters;
  Connection_t* connections; /* Array of connections */
  size_t n_connections;

  /* External interface (direct pointers) */
  Filter_t* input_filter;  /* Which filter to expose as input */
  size_t input_port;       /* Which port (default: 0) */
  Filter_t* output_filter; /* Which filter to expose as output */
  size_t output_port;      /* Which port (default: 0) */
} Pipeline_config_t;

/* Pipeline input declaration - allows specifying expected properties */
typedef struct {
  Filter_t* filter;                             /* Filter acting as pipeline input */
  PropertyTable_t expected_properties[MAX_INPUTS]; /* Properties this input expects */
  size_t n_inputs;                             /* Number of input ports on this filter */
} PipelineInput_t;

typedef struct _Pipeline_t {
  Filter_t base; /* MUST be first member - enables standard filter interface */

  /* Filter management */
  Filter_t** filters; /* Array of filter pointers */
  size_t n_filters;

  /* Connection specification (direct pointer references) */
  struct {
    Filter_t* from_filter;
    size_t from_port;
    Filter_t* to_filter;
    size_t to_port;
  } * connections;
  size_t n_connections;

  /* External interface mapping (direct pointers) */
  Filter_t* input_filter;  /* Which filter provides pipeline input */
  size_t input_port;       /* Which port of that filter */
  Filter_t* output_filter; /* Which filter provides pipeline output  */
  size_t output_port;      /* Which port of that filter */

  /* Pipeline input property declarations (Option C) */
  PipelineInput_t pipeline_inputs[MAX_INPUTS];
  size_t n_pipeline_inputs;

} Pipeline_t;

/* Standard bpipe2 initialization pattern */
Bp_EC pipeline_init(Pipeline_t* pipe, Pipeline_config_t config);

/* Declare a filter as a pipeline input with expected properties
 * This allows the pipeline to propagate properties correctly even when the
 * filter has no upstream connections within the pipeline
 * @param pipeline: The pipeline to add the input to
 * @param filter: The filter to declare as a pipeline input
 * @param expected_properties: Array of properties expected for each input port
 * @param n_inputs: Number of input ports (size of expected_properties array)
 * @return: Bp_EC_OK on success, error code otherwise
 */
Bp_EC pipeline_add_input(Pipeline_t* pipeline, Filter_t* filter,
                         const PropertyTable_t* expected_properties,
                         size_t n_inputs);

/* Validate properties throughout the pipeline
 * This function propagates properties through all filters and validates
 * constraints
 * @param pipe: The pipeline to validate
 * @param error_msg: Buffer for error message (optional, can be NULL)
 * @param error_msg_size: Size of error message buffer
 * @return: Bp_EC_OK if validation passes, error code otherwise
 */
Bp_EC pipeline_validate_properties(Pipeline_t* pipe, char* error_msg,
                                   size_t error_msg_size);

/* Standard filter lifecycle (inherited from Filter_t) */
/* filt_start(), filt_stop(), filt_deinit() work automatically */

#endif /* BPIPE_PIPELINE_H */