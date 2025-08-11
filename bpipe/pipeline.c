#include "pipeline.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Forward declarations */
static Bp_EC pipeline_start(Filter_t* self);
static Bp_EC pipeline_stop(Filter_t* self);
static Bp_EC pipeline_deinit(Filter_t* self);
static Bp_EC pipeline_sink_connect(Filter_t* self, size_t output_port,
                                   Batch_buff_t* sink);
static Bp_EC pipeline_describe(Filter_t* self, char* buffer, size_t size);
static bool pipeline_contains_filter(Pipeline_t* pipe, Filter_t* filter);
static void* pipeline_worker(void* arg);

Bp_EC pipeline_init(Pipeline_t* pipe, Pipeline_config_t config)
{
  if (!pipe || !config.filters || config.n_filters == 0)
    return Bp_EC_NULL_POINTER;
  if (!config.input_filter || !config.output_filter) return Bp_EC_NULL_POINTER;

  /* Initialize base filter */
  Core_filt_config_t core_config = {
      .name = config.name,
      .filt_type = FILT_T_PIPELINE,
      .size = sizeof(Pipeline_t),
      .n_inputs = 1,            /* Pipeline has single input */
      .max_supported_sinks = 1, /* Pipeline has single output */
      .buff_config = config.buff_config,
      .timeout_us = config.timeout_us,
      .worker = pipeline_worker /* Dummy worker - required by filt_init */
  };

  Bp_EC err = filt_init(&pipe->base, core_config);
  if (err != Bp_EC_OK) return err;

  /* Copy filter pointers (no string duplication needed) */
  pipe->filters = malloc(config.n_filters * sizeof(Filter_t*));
  if (!pipe->filters) {
    filt_deinit(&pipe->base);
    return Bp_EC_ALLOC;
  }

  memcpy(pipe->filters, config.filters, config.n_filters * sizeof(Filter_t*));
  pipe->n_filters = config.n_filters;

  /* Validate all filters are in our filter list */
  for (size_t i = 0; i < config.n_connections; i++) {
    if (!pipeline_contains_filter(pipe, config.connections[i].from_filter) ||
        !pipeline_contains_filter(pipe, config.connections[i].to_filter)) {
      free(pipe->filters);
      filt_deinit(&pipe->base);
      return Bp_EC_INVALID_CONFIG;
    }
  }

  /* Copy connections (direct pointer references) */
  if (config.n_connections > 0) {
    pipe->connections =
        malloc(config.n_connections * sizeof(*pipe->connections));
    if (!pipe->connections) {
      free(pipe->filters);
      filt_deinit(&pipe->base);
      return Bp_EC_ALLOC;
    }

    for (size_t i = 0; i < config.n_connections; i++) {
      pipe->connections[i].from_filter = config.connections[i].from_filter;
      pipe->connections[i].from_port = config.connections[i].from_port;
      pipe->connections[i].to_filter = config.connections[i].to_filter;
      pipe->connections[i].to_port = config.connections[i].to_port;
    }
    pipe->n_connections = config.n_connections;

    /* Create internal connections using filt_sink_connect */
    for (size_t i = 0; i < config.n_connections; i++) {
      Connection_t* conn = &config.connections[i];
      err = filt_sink_connect(conn->from_filter, conn->from_port,
                              conn->to_filter->input_buffers[conn->to_port]);
      if (err != Bp_EC_OK) {
        /* Clean up on failure */
        free(pipe->connections);
        free(pipe->filters);
        filt_deinit(&pipe->base);
        return err;
      }
    }
  } else {
    pipe->connections = NULL;
    pipe->n_connections = 0;
  }

  /* Initialize pipeline inputs (empty by default) */
  pipe->n_external_inputs = 0;
  /* pipeline_inputs is a fixed array, no need to set to NULL */

  /* Set up external interface (direct pointer references) */
  pipe->input_filter = config.input_filter;
  pipe->input_port = config.input_port;
  pipe->output_filter = config.output_filter;
  pipe->output_port = config.output_port;

  /* Validate external interface filters are in our pipeline */
  if (!pipeline_contains_filter(pipe, config.input_filter) ||
      !pipeline_contains_filter(pipe, config.output_filter)) {
    if (pipe->connections) free(pipe->connections);
    free(pipe->filters);
    filt_deinit(&pipe->base);
    return Bp_EC_INVALID_CONFIG;
  }

  /* Share input buffer with designated input filter (zero-copy) */
  /* First, clean up the allocated buffer from filt_init */
  if (pipe->base.input_buffers[0]) {
    bb_deinit(pipe->base.input_buffers[0]);
    free(pipe->base.input_buffers[0]);
    /* Important: set to shared buffer before filt_deinit can access it */
  }
  pipe->base.input_buffers[0] =
      pipe->input_filter->input_buffers[pipe->input_port];

  /* Override operations */
  pipe->base.ops.start = pipeline_start;
  pipe->base.ops.stop = pipeline_stop;
  pipe->base.ops.deinit = pipeline_deinit;
  pipe->base.ops.sink_connect = pipeline_sink_connect;
  pipe->base.ops.describe = pipeline_describe;

  /* Set worker to NULL - pipeline doesn't need its own worker thread */
  pipe->base.worker = NULL;

  return Bp_EC_OK;
}

/* Helper function to validate filter is in pipeline */
static bool pipeline_contains_filter(Pipeline_t* pipe, Filter_t* filter)
{
  for (size_t i = 0; i < pipe->n_filters; i++) {
    if (pipe->filters[i] == filter) {
      return true;
    }
  }
  return false;
}

/* Pipeline leverages existing filter lifecycle management */
static Bp_EC pipeline_start(Filter_t* self)
{
  Pipeline_t* pipe = (Pipeline_t*) self;

  /* Phase 0.4: Validate properties before starting filters */
  char error_msg[256];
  /* Root pipeline validation - no external inputs */
  Bp_EC validation_err =
      pipeline_validate_properties(pipe, NULL, 0, error_msg, sizeof(error_msg));
  if (validation_err != Bp_EC_OK) {
    /* Log error message if validation fails */
    if (strlen(error_msg) > 0) {
      fprintf(stderr, "Pipeline validation failed: %s\n", error_msg);
    }
    return validation_err;
  }

  /* Start internal filters (order doesn't matter - they're already connected)
   */
  for (size_t i = 0; i < pipe->n_filters; i++) {
    Bp_EC err = filt_start(pipe->filters[i]);
    if (err != Bp_EC_OK) {
      /* Stop already started filters on failure */
      for (size_t j = i; j > 0; j--) {
        filt_stop(pipe->filters[j - 1]);
      }
      return err;
    }
  }

  /* Pipeline itself has no worker thread - internal filters do the work */
  atomic_store(&pipe->base.running, true);
  return Bp_EC_OK;
}

static Bp_EC pipeline_stop(Filter_t* self)
{
  Pipeline_t* pipe = (Pipeline_t*) self;

  /* Signal stop */
  atomic_store(&pipe->base.running, false);

  /* Stop internal filters in reverse order (for clean shutdown) */
  for (size_t i = pipe->n_filters; i > 0; i--) {
    filt_stop(pipe->filters[i - 1]);
  }

  return Bp_EC_OK;
}

static Bp_EC pipeline_sink_connect(Filter_t* self, size_t output_port,
                                   Batch_buff_t* sink)
{
  Pipeline_t* pipe = (Pipeline_t*) self;

  /* Validate output port - pipeline has single output */
  if (output_port != 0) {
    return Bp_EC_INVALID_SINK_IDX;
  }

  /* Forward connection to the designated output filter */
  return filt_sink_connect(pipe->output_filter, pipe->output_port, sink);
}

static Bp_EC pipeline_deinit(Filter_t* self)
{
  Pipeline_t* pipe = (Pipeline_t*) self;

  if (pipe->filters) {
    /* Free filter pointer array (filters themselves are managed externally) */
    free(pipe->filters);
    pipe->filters = NULL;
  }

  if (pipe->connections) {
    free(pipe->connections);
    pipe->connections = NULL;
  }

  /* Important: Set input buffer to NULL to prevent double-free
   * The buffer is shared with input_filter and will be freed there */
  pipe->base.input_buffers[0] = NULL;

  /* Base filter cleanup is handled by caller (filt_deinit) */
  return Bp_EC_OK;
}

/* Enhanced describe function for debugging */
static Bp_EC pipeline_describe(Filter_t* self, char* buffer, size_t size)
{
  Pipeline_t* pipe = (Pipeline_t*) self;

  size_t written =
      snprintf(buffer, size, "Pipeline '%s': %zu filters, %zu connections\n",
               self->name, pipe->n_filters, pipe->n_connections);

  /* Show topology (use filter names from pointers) */
  if (written < size && pipe->input_filter && pipe->output_filter) {
    written += snprintf(buffer + written, size - written,
                        "Input: %s[%zu] -> Output: %s[%zu]\n",
                        pipe->input_filter->name, pipe->input_port,
                        pipe->output_filter->name, pipe->output_port);
  }

  /* Show filter states with names (get names from filter pointers) */
  for (size_t i = 0; i < pipe->n_filters && written < size; i++) {
    Filter_t* f = pipe->filters[i];
    const char* status = atomic_load(&f->running) ? "running" : "stopped";
    const char* error = f->worker_err_info.ec == Bp_EC_OK
                            ? "OK"
                            : err_lut[f->worker_err_info.ec];
    written += snprintf(buffer + written, size - written, "  %s: %s (%s)\n",
                        f->name, status, error);
  }

  return Bp_EC_OK;
}

/* Dummy worker function - pipeline uses component filter workers */
static void* pipeline_worker(void* arg)
{
  /* Pipeline doesn't need its own worker thread.
   * All work is done by the component filter workers.
   * This function exists only to satisfy the framework requirement.
   */
  (void) arg; /* Suppress unused parameter warning */
  return NULL;
}

/* Declare which filter port receives an external input */
Bp_EC pipeline_declare_external_input(Pipeline_t* pipeline,
                                      size_t external_index,
                                      Filter_t* filter,
                                      size_t filter_port)
{
  if (!pipeline || !filter) {
    return Bp_EC_NULL_POINTER;
  }
  
  if (external_index >= MAX_INPUTS) {
    return Bp_EC_INVALID_CONFIG;
  }
  
  /* Verify the filter is in our pipeline */
  if (!pipeline_contains_filter(pipeline, filter)) {
    return Bp_EC_INVALID_CONFIG;
  }
  
  /* Store the external input mapping */
  pipeline->external_input_mappings[external_index].filter = filter;
  pipeline->external_input_mappings[external_index].port = filter_port;
  
  /* Update count if necessary */
  if (external_index >= pipeline->n_external_inputs) {
    pipeline->n_external_inputs = external_index + 1;
  }
  
  return Bp_EC_OK;
}

/* Helper function for topological sort using DFS */
static Bp_EC topological_sort_visit(Filter_t* filter, Pipeline_t* pipe,
                                    Filter_t** sorted, size_t* n_sorted,
                                    bool* visited, bool* visiting)
{
  /* Find the index of this filter */
  size_t filter_idx = 0;
  for (size_t i = 0; i < pipe->n_filters; i++) {
    if (pipe->filters[i] == filter) {
      filter_idx = i;
      break;
    }
  }
  
  if (visiting[filter_idx]) {
    /* Cycle detected */
    return Bp_EC_INVALID_CONFIG;
  }
  
  if (visited[filter_idx]) {
    /* Already processed */
    return Bp_EC_OK;
  }
  
  visiting[filter_idx] = true;
  
  /* Visit all filters that depend on this filter (downstream filters) */
  for (size_t i = 0; i < pipe->n_connections; i++) {
    if (pipe->connections[i].from_filter == filter) {
      Bp_EC err = topological_sort_visit(pipe->connections[i].to_filter, pipe,
                                         sorted, n_sorted, visited, visiting);
      if (err != Bp_EC_OK) {
        return err;
      }
    }
  }
  
  visiting[filter_idx] = false;
  visited[filter_idx] = true;
  
  /* Add to sorted list (in reverse order - we'll process from end) */
  sorted[(*n_sorted)++] = filter;
  
  return Bp_EC_OK;
}

/* Perform topological sort of filters in the pipeline */
static Bp_EC topological_sort(Pipeline_t* pipe, Filter_t** sorted,
                              size_t* n_sorted)
{
  if (!pipe || !sorted || !n_sorted) {
    return Bp_EC_NULL_POINTER;
  }
  
  bool visited[pipe->n_filters];
  bool visiting[pipe->n_filters];
  memset(visited, 0, sizeof(visited));
  memset(visiting, 0, sizeof(visiting));
  *n_sorted = 0;
  
  /* Start with source filters (no inputs) and pipeline inputs */
  for (size_t i = 0; i < pipe->n_filters; i++) {
    Filter_t* filter = pipe->filters[i];
    bool is_source = (filter->n_input_buffers == 0);
    bool is_pipeline_input = false;
    
    /* Check if it's a declared pipeline input */
    for (size_t j = 0; j < pipe->n_external_inputs; j++) {
      if (pipe->external_input_mappings[j].filter == filter) {
        is_pipeline_input = true;
        break;
      }
    }
    
    /* Also check for filters with no upstream connections in the pipeline */
    bool has_upstream = false;
    for (size_t j = 0; j < pipe->n_connections; j++) {
      if (pipe->connections[j].to_filter == filter) {
        has_upstream = true;
        break;
      }
    }
    
    if (is_source || is_pipeline_input || !has_upstream) {
      Bp_EC err = topological_sort_visit(filter, pipe, sorted, n_sorted,
                                         visited, visiting);
      if (err != Bp_EC_OK) {
        return err;
      }
    }
  }
  
  /* Check if all filters were visited */
  if (*n_sorted != pipe->n_filters) {
    /* Some filters are unreachable or there's a disconnected component */
    for (size_t i = 0; i < pipe->n_filters; i++) {
      if (!visited[i]) {
        Bp_EC err = topological_sort_visit(pipe->filters[i], pipe, sorted,
                                           n_sorted, visited, visiting);
        if (err != Bp_EC_OK) {
          return err;
        }
      }
    }
  }
  
  /* Reverse the sorted array (we built it in reverse order) */
  for (size_t i = 0; i < *n_sorted / 2; i++) {
    Filter_t* tmp = sorted[i];
    sorted[i] = sorted[*n_sorted - 1 - i];
    sorted[*n_sorted - 1 - i] = tmp;
  }
  
  return Bp_EC_OK;
}

/* Helper to check if a filter has an external input mapping */
static PropertyTable_t* find_external_input(const Pipeline_t* pipe,
                                           PropertyTable_t* external_inputs,
                                           size_t n_external_inputs,
                                           Filter_t* filter,
                                           size_t input_port)
{
  if (!external_inputs || n_external_inputs == 0) {
    return NULL;
  }
  
  /* Check if this filter:port has an external input mapping */
  for (size_t i = 0; i < pipe->n_external_inputs && i < n_external_inputs; i++) {
    if (pipe->external_input_mappings[i].filter == filter &&
        pipe->external_input_mappings[i].port == input_port) {
      return &external_inputs[i];
    }
  }
  return NULL;
}

/* Validate properties throughout the pipeline
 * Phase 2: Full DAG support with topological sort
 */
Bp_EC pipeline_validate_properties(const Pipeline_t* pipeline,
                                   PropertyTable_t* external_inputs,
                                   size_t n_external_inputs,
                                   char* error_msg,
                                   size_t error_msg_size)
{
  if (!pipeline) {
    return Bp_EC_NULL_POINTER;
  }

  /* Clear error message if provided */
  if (error_msg && error_msg_size > 0) {
    error_msg[0] = '\0';
  }

  /* Check if this is a root pipeline (no external inputs) */
  bool is_root = (external_inputs == NULL || n_external_inputs == 0);
  
  if (is_root) {
    /* Root pipeline must have at least one source filter */
    bool has_source = false;
    for (size_t i = 0; i < pipeline->n_filters; i++) {
      if (pipeline->filters[i] && pipeline->filters[i]->n_input_buffers == 0) {
        has_source = true;
        break;
      }
    }
    if (!has_source) {
      if (error_msg && error_msg_size > 0) {
        snprintf(error_msg, error_msg_size,
                 "Root pipeline has no source filters. A root pipeline must contain "
                 "at least one source filter to generate data.");
      }
      return Bp_EC_INVALID_CONFIG;
    }
  }

  /* Phase 2: Full DAG support with topological sort */
  
  /* Perform topological sort */
  Filter_t* sorted[pipeline->n_filters];
  size_t n_sorted = 0;
  Bp_EC sort_err = topological_sort((Pipeline_t*)pipeline, sorted, &n_sorted);
  if (sort_err != Bp_EC_OK) {
    if (error_msg && error_msg_size > 0) {
      snprintf(error_msg, error_msg_size, "Pipeline contains a cycle");
    }
    return sort_err;
  }

  /* Process filters in topological order */
  for (size_t i = 0; i < n_sorted; i++) {
    Filter_t* filter = sorted[i];
    if (!filter) continue;

    /* Check if this is a source filter (no inputs) */
    bool is_source = (filter->n_input_buffers == 0);

    if (is_source) {
      /* Source filter: propagate from UNKNOWN inputs */
      PropertyTable_t unknown = prop_table_init();
      prop_set_all_unknown(&unknown);

      /* Propagate properties through the filter for each output port */
      for (uint32_t port = 0; port < filter->n_outputs; port++) {
        filter->output_properties[port] = prop_propagate(NULL, 0, &filter->contract, port);
      }
    } else {
      /* Transform/sink filter: collect input properties and validate */
      
      /* Collect input properties for all input ports */
      for (size_t input_port = 0; input_port < filter->n_input_buffers; input_port++) {
        /* First check if this filter:port receives an external input */
        PropertyTable_t* external_input_props = 
            find_external_input(pipeline, external_inputs, n_external_inputs,
                               filter, input_port);
        
        if (external_input_props) {
          /* Use the provided external input properties */
          filter->input_properties[input_port] = *external_input_props;
        } else {
          /* Find the upstream filter that connects to this input port */
          Filter_t* upstream = NULL;
          size_t upstream_port = 0;
          bool found_connection = false;

          for (size_t j = 0; j < pipeline->n_connections; j++) {
            if (pipeline->connections[j].to_filter == filter &&
                pipeline->connections[j].to_port == input_port) {
              upstream = pipeline->connections[j].from_filter;
              upstream_port = pipeline->connections[j].from_port;
              found_connection = true;
              break;
            }
          }

          if (found_connection && upstream) {
            /* Validate connection constraints */
            Bp_EC err = prop_validate_connection(
                &upstream->output_properties[upstream_port],
                &filter->contract, input_port,
                error_msg, error_msg_size);
            
            if (err != Bp_EC_OK) {
              /* Add context to error message */
              if (error_msg && error_msg_size > 0) {
                size_t len = strlen(error_msg);
                if (len < error_msg_size - 1) {
                  snprintf(error_msg + len, error_msg_size - len,
                           " (Connection: %s[%zu] -> %s[%zu])",
                           upstream->name, upstream_port,
                           filter->name, input_port);
                }
              }
              return err;
            }
            
            /* Store input properties for this connection */
            filter->input_properties[input_port] = 
                upstream->output_properties[upstream_port];
          } else if (filter == pipeline->input_filter && input_port == pipeline->input_port) {
            /* This is the pipeline's input port - use UNKNOWN for root, error for nested */
            if (is_root) {
              PropertyTable_t unknown = prop_table_init();
              prop_set_all_unknown(&unknown);
              filter->input_properties[input_port] = unknown;
            } else {
              /* Nested pipeline should have external input declared for this */
              if (error_msg && error_msg_size > 0) {
                snprintf(error_msg, error_msg_size,
                         "Pipeline input filter '%s' port %zu requires external input but none provided",
                         filter->name, input_port);
              }
              return Bp_EC_INVALID_CONFIG;
            }
          } else {
            /* No upstream connection and not a pipeline input - error */
            if (error_msg && error_msg_size > 0) {
              snprintf(error_msg, error_msg_size,
                       "Filter '%s' input port %zu has no upstream connection",
                       filter->name, input_port);
            }
            return Bp_EC_INVALID_CONFIG;
          }
        }
      }
      
      /* Validate multi-input alignment constraints if applicable */
      for (size_t j = 0; j < filter->n_input_constraints; j++) {
        const InputConstraint_t* constraint = &filter->input_constraints[j];
        if (constraint->op == CONSTRAINT_OP_MULTI_INPUT_ALIGNED) {
          /* Check alignment across specified input ports */
          Property_t* first_prop = NULL;
          size_t first_port = 0;
          
          for (size_t port = 0; port < filter->n_input_buffers; port++) {
            if (constraint->input_mask & (1 << port)) {
              Property_t* prop = &filter->input_properties[port].properties[constraint->property];
              
              if (!first_prop) {
                first_prop = prop;
                first_port = port;
              } else {
                /* Compare with first property */
                if (prop->known != first_prop->known ||
                    (prop->known && memcmp(&prop->value, &first_prop->value, sizeof(prop->value)) != 0)) {
                  if (error_msg && error_msg_size > 0) {
                    snprintf(error_msg, error_msg_size,
                             "Multi-input alignment constraint failed for property '%s' on filter '%s': "
                             "input port %zu differs from port %zu",
                             prop_get_name(constraint->property), filter->name,
                             port, first_port);
                  }
                  return Bp_EC_PROPERTY_MISMATCH;
                }
              }
            }
          }
        }
      }
      
      /* Propagate properties through this filter for each output port */
      for (uint32_t port = 0; port < filter->n_outputs; port++) {
        filter->output_properties[port] = prop_propagate(
            filter->input_properties, filter->n_input_buffers,
            &filter->contract, port);
      }
    }
  }

  /* All filters validated successfully */
  return Bp_EC_OK;
}