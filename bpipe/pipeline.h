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

/* External input mapping - maps external inputs to internal filter ports */
typedef struct {
  Filter_t* filter;  /* Target filter to receive external input */
  size_t port;       /* Which input port on that filter */
} ExternalInputMapping_t;

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

  /* External input mappings - which filters receive external inputs */
  ExternalInputMapping_t external_input_mappings[MAX_INPUTS];
  size_t n_external_inputs;

} Pipeline_t;

/* Standard bpipe2 initialization pattern */
Bp_EC pipeline_init(Pipeline_t* pipe, Pipeline_config_t config);

/* Declare which filter port receives an external input
 * This establishes the mapping: external_inputs[index] -> filter:port
 * @param pipeline: The pipeline to configure
 * @param external_index: Index in the external_inputs array (0-based)
 * @param filter: The filter that will receive this external input
 * @param filter_port: Which input port on that filter (0-based)
 * @return: Bp_EC_OK on success, error code otherwise
 */
Bp_EC pipeline_declare_external_input(Pipeline_t* pipeline,
                                      size_t external_index,
                                      Filter_t* filter,
                                      size_t filter_port);

/* Validate properties throughout the pipeline
 * This function propagates properties through all filters and validates constraints.
 * For root pipelines (no external inputs), pass NULL for external_inputs.
 * For nested pipelines, provide the external input properties.
 * @param pipeline: The pipeline to validate
 * @param external_inputs: Array of property tables for external inputs (NULL for root)
 * @param n_external_inputs: Number of external inputs (0 for root)
 * @param error_msg: Buffer for error message (optional, can be NULL)
 * @param error_msg_size: Size of error message buffer
 * @return: Bp_EC_OK if validation passes, error code otherwise
 */
Bp_EC pipeline_validate_properties(const Pipeline_t* pipeline,
                                   PropertyTable_t* external_inputs,
                                   size_t n_external_inputs,
                                   char* error_msg,
                                   size_t error_msg_size);

/* Standard filter lifecycle (inherited from Filter_t) */
/* filt_start(), filt_stop(), filt_deinit() work automatically */

#endif /* BPIPE_PIPELINE_H */