#include "pipeline.h"
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
static Bp_EC pipeline_start(Filter_t* self);
static Bp_EC pipeline_stop(Filter_t* self);
static Bp_EC pipeline_deinit(Filter_t* self);
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
      .worker = pipeline_worker /* Dummy worker - pipeline uses component filter
                                   workers */
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
  }
  pipe->base.input_buffers[0] =
      pipe->input_filter->input_buffers[pipe->input_port];

  /* Override operations */
  pipe->base.ops.start = pipeline_start;
  pipe->base.ops.stop = pipeline_stop;
  pipe->base.ops.deinit = pipeline_deinit;
  pipe->base.ops.describe = pipeline_describe;

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

  /* Start internal filters (order doesn't matter - they're already connected)
   */
  for (size_t i = 0; i < pipe->n_filters; i++) {
    Bp_EC err = filt_start(pipe->filters[i]);
    if (err != Bp_EC_OK) {
      /* Stop already started filters on failure */
      for (int j = i - 1; j >= 0; j--) {
        filt_stop(pipe->filters[j]);
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
  for (int i = pipe->n_filters - 1; i >= 0; i--) {
    filt_stop(pipe->filters[i]);
  }

  return Bp_EC_OK;
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