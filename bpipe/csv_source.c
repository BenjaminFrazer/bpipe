#define _GNU_SOURCE  // For strdup
#include "csv_source.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"

#define LINE_BUFFER_SIZE 4096

// Define specific error codes using existing ones
#define Bp_EC_FILE_NOT_FOUND Bp_EC_INVALID_CONFIG
#define Bp_EC_IO_ERROR Bp_EC_INVALID_DATA
#define Bp_EC_PERMISSION_DENIED Bp_EC_INVALID_CONFIG
#define Bp_EC_PARSE_ERROR Bp_EC_INVALID_DATA
#define Bp_EC_FORMAT_ERROR Bp_EC_INVALID_DATA
#define Bp_EC_COLUMN_NOT_FOUND Bp_EC_INVALID_CONFIG

/* Future extensions to consider:
 * - Support for different timestamp formats (ISO8601, Unix epoch, custom
 * formats)
 * - Support for compressed files (gzip, bzip2, etc.)
 * - Support for remote files (HTTP/HTTPS URLs)
 * - Support for streaming input (stdin, named pipes)
 * - Configurable buffer sizes
 * - Multi-threaded parsing for large files
 */

static inline bool is_power_of_two(size_t n)
{
  return n > 0 && (n & (n - 1)) == 0;
}

static Bp_EC parse_header(CsvSource_t* self);
static Bp_EC parse_line(CsvSource_t* self, char* line, uint64_t* timestamp,
                        double* values);
static void* csvsource_worker(void* arg);
static Bp_EC csvsource_describe(Filter_t* self, char* buffer, size_t size);
static Bp_EC csvsource_get_stats(Filter_t* self, void* stats);

Bp_EC csvsource_init(CsvSource_t* self, CsvSource_config_t config)
{
  if (!self || !config.file_path || !config.ts_column_name) {
    return Bp_EC_NULL_POINTER;
  }

  memset(self, 0, sizeof(CsvSource_t));

  self->delimiter = config.delimiter ? config.delimiter : ',';
  self->has_header = config.has_header;
  self->ts_column_name = config.ts_column_name;
  self->detect_regular_timing = config.detect_regular_timing;
  self->regular_threshold_ns =
      config.regular_threshold_ns ? config.regular_threshold_ns : 1000;
  self->loop = config.loop;
  self->skip_invalid = config.skip_invalid;

  // Store file path
  self->file_path = strdup(config.file_path);
  if (!self->file_path) {
    return Bp_EC_MALLOC_FAIL;
  }

  size_t i;
  for (i = 0; i < BP_CSV_MAX_COLUMNS && config.data_column_names[i] != NULL;
       i++) {
    self->data_column_names[i] = config.data_column_names[i];
  }
  self->n_data_columns = i;

  if (self->n_data_columns == 0) {
    free(self->file_path);
    return Bp_EC_INVALID_CONFIG;
  }

  self->ts_column_index = -1;
  for (i = 0; i < self->n_data_columns; i++) {
    self->data_column_indices[i] = -1;
  }

  self->file = fopen(config.file_path, "r");
  if (!self->file) {
    free(self->file_path);
    if (errno == ENOENT) {
      return Bp_EC_FILE_NOT_FOUND;
    } else if (errno == EACCES) {
      return Bp_EC_PERMISSION_DENIED;
    }
    return Bp_EC_IO_ERROR;
  }

  self->line_buffer_size = LINE_BUFFER_SIZE;
  self->line_buffer = malloc(self->line_buffer_size);
  if (!self->line_buffer) {
    fclose(self->file);
    free(self->file_path);
    return Bp_EC_MALLOC_FAIL;
  }

  // No longer need timestamp_buffer or data_accumulation_buffer
  // We write directly to batches now

  if (self->has_header) {
    Bp_EC err = parse_header(self);
    if (err != Bp_EC_OK) {
      csvsource_destroy(self);
      return err;
    }
  }

  // Create dummy buffer config for unused input buffers
  // Since this is a source filter (n_inputs = 0), this config is never used
  BatchBuffer_config dummy_buff_config = {
      .dtype = DTYPE_FLOAT,      // Dummy value
      .batch_capacity_expo = 6,  // Dummy value (64 samples)
      .ring_capacity_expo = 8,   // Dummy value (256 batches)
      .overflow_behaviour = OVERFLOW_BLOCK};

  Core_filt_config_t filter_config = {
      .name = config.name,
      .filt_type = FILT_T_NDEF,  // Source filter (no inputs)
      .size = sizeof(CsvSource_t),
      .n_inputs = 0,
      .max_supported_sinks = self->n_data_columns,  // One sink per data column
      .buff_config =
          dummy_buff_config,  // Required by API but unused for source filters
      .timeout_us = config.timeout_us,
      .worker = csvsource_worker};

  Bp_EC err = filt_init(&self->base, filter_config);
  if (err != Bp_EC_OK) {
    return err;
  }

  // Set operations
  self->base.ops.describe = csvsource_describe;
  self->base.ops.get_stats = csvsource_get_stats;

  return Bp_EC_OK;
}

static Bp_EC parse_header(CsvSource_t* self)
{
  if (!fgets(self->line_buffer, self->line_buffer_size, self->file)) {
    return Bp_EC_INVALID_DATA;
  }

  // Check if header line was truncated
  size_t len = strlen(self->line_buffer);
  if (len > 0 && self->line_buffer[len - 1] != '\n' && !feof(self->file)) {
    return Bp_EC_INVALID_DATA;
  }

  self->current_line = 1;
  if (len > 0 && self->line_buffer[len - 1] == '\n') {
    self->line_buffer[len - 1] = '\0';
  }

  size_t n_columns = 0;
  char* header_copy = strdup(self->line_buffer);
  if (!header_copy) {
    return Bp_EC_MALLOC_FAIL;
  }

  char* token = strtok(header_copy, &self->delimiter);
  while (token != NULL && n_columns < BP_CSV_MAX_COLUMNS) {
    n_columns++;
    token = strtok(NULL, &self->delimiter);
  }

  self->n_header_columns = n_columns;
  self->header_names = calloc(n_columns, sizeof(char*));
  if (!self->header_names) {
    free(header_copy);
    return Bp_EC_MALLOC_FAIL;
  }

  strcpy(header_copy, self->line_buffer);
  token = strtok(header_copy, &self->delimiter);
  int col_idx = 0;

  while (token != NULL && col_idx < n_columns) {
    self->header_names[col_idx] = strdup(token);

    if (strcmp(token, self->ts_column_name) == 0) {
      self->ts_column_index = col_idx;
    }

    for (size_t i = 0; i < self->n_data_columns; i++) {
      if (strcmp(token, self->data_column_names[i]) == 0) {
        self->data_column_indices[i] = col_idx;
      }
    }

    token = strtok(NULL, &self->delimiter);
    col_idx++;
  }

  free(header_copy);

  if (self->ts_column_index == -1) {
    return Bp_EC_COLUMN_NOT_FOUND;
  }

  for (size_t i = 0; i < self->n_data_columns; i++) {
    if (self->data_column_indices[i] == -1) {
      return Bp_EC_COLUMN_NOT_FOUND;
    }
  }

  return Bp_EC_OK;
}

static Bp_EC parse_line(CsvSource_t* self, char* line, uint64_t* timestamp,
                        double* values)
{
  size_t len = strlen(line);
  if (len > 0 && line[len - 1] == '\n') {
    line[len - 1] = '\0';
  }

  char* line_copy = strdup(line);
  if (!line_copy) {
    return Bp_EC_MALLOC_FAIL;
  }

  memset(self->parse_buffer, 0, sizeof(self->parse_buffer));

  char* token = strtok(line_copy, &self->delimiter);
  int col_idx = 0;

  while (token != NULL && col_idx < self->n_header_columns) {
    if (col_idx == self->ts_column_index) {
      char* endptr;
      errno = 0;
      *timestamp = strtoull(token, &endptr, 10);
      if (errno != 0 || *endptr != '\0') {
        free(line_copy);
        return Bp_EC_INVALID_DATA;
      }
    } else {
      char* endptr;
      errno = 0;
      self->parse_buffer[col_idx] = strtod(token, &endptr);
      if (errno != 0 || *endptr != '\0') {
        free(line_copy);
        return Bp_EC_INVALID_DATA;
      }
    }

    token = strtok(NULL, &self->delimiter);
    col_idx++;
  }

  for (size_t i = 0; i < self->n_data_columns; i++) {
    values[i] = self->parse_buffer[self->data_column_indices[i]];
  }

  free(line_copy);
  return Bp_EC_OK;
}

// Define the batch state structure
typedef struct {
  Batch_t* batches[BP_CSV_MAX_COLUMNS];  // Current batch for each column
  uint64_t batch_start_time;
  uint64_t expected_delta;
  bool delta_established;
} BatchState;

// Helper to check if we need to submit current batches and get new ones
static bool need_new_batches(const CsvSource_t* self, const BatchState* state,
                             uint64_t timestamp)
{
  // No current batches
  if (!state->batches[0]) {
    return true;
  }

  size_t current_samples = state->batches[0]->tail;

  // Force single-sample batches for irregular mode
  if (!self->detect_regular_timing && current_samples > 0) {
    return true;
  }

  // Check if batch is full - get batch size from the sink
  size_t batch_capacity = (1 << self->base.sinks[0]->batch_capacity_expo);
  if (current_samples >= batch_capacity) {
    return true;
  }

  // Check timing pattern for regular mode
  if (self->detect_regular_timing && current_samples > 1) {
    uint64_t expected_time =
        state->batch_start_time + (current_samples * state->expected_delta);
    if (timestamp != expected_time) {
      return true;  // Timing break
    }
  }

  return false;
}

// Helper to submit current batches and get new ones
static Bp_EC submit_and_get_new_batches(CsvSource_t* self, BatchState* state)
{
  // Submit current batches if they have data
  if (state->batches[0] && state->batches[0]->tail > 0) {
    uint64_t period_ns = state->delta_established ? state->expected_delta : 0;

    for (size_t col = 0; col < self->n_data_columns; col++) {
      Batch_t* batch = state->batches[col];
      batch->t_ns = state->batch_start_time;
      batch->period_ns = period_ns;
      batch->head = 0;  // Data starts at index 0
      // tail is already set to the number of samples
      batch->ec = Bp_EC_OK;
      bb_submit(self->base.sinks[col], self->base.timeout_us);
    }

    // Update metrics
    self->base.metrics.samples_processed += state->batches[0]->tail;
    self->base.metrics.n_batches++;
  }

  // Get new batches
  for (size_t col = 0; col < self->n_data_columns; col++) {
    state->batches[col] = bb_get_head(self->base.sinks[col]);
    if (!state->batches[col]) {
      return Bp_EC_TIMEOUT;  // bb_get_head should never return NULL
    }
    state->batches[col]->head = 0;
    state->batches[col]->tail = 0;
  }

  state->delta_established = false;
  return Bp_EC_OK;
}

// Helper to write sample directly to batches
static void write_sample_to_batches(CsvSource_t* self, BatchState* state,
                                    uint64_t timestamp, const double* values)
{
  size_t idx = state->batches[0]->tail;

  // First sample in batch sets the start time
  if (idx == 0) {
    state->batch_start_time = timestamp;
    state->delta_established = false;
  } else if (idx == 1) {
    // Second sample establishes delta
    state->expected_delta = timestamp - state->batch_start_time;
    state->delta_established = true;
  }

  // Write value to each column's batch at current tail position
  for (size_t col = 0; col < self->n_data_columns; col++) {
    Batch_t* batch = state->batches[col];

    switch (self->base.sinks[col]->dtype) {
      case DTYPE_FLOAT:
        ((float*) batch->data)[idx] = (float) values[col];
        break;
      case DTYPE_I32:
        ((int32_t*) batch->data)[idx] = (int32_t) values[col];
        break;
      case DTYPE_U32:
        ((uint32_t*) batch->data)[idx] = (uint32_t) values[col];
        break;
      default:
        break;
    }

    // Increment tail for this batch
    batch->tail++;
  }
}

static void* csvsource_worker(void* arg)
{
  CsvSource_t* self = (CsvSource_t*) arg;

  // Validate we have the correct number of sinks connected
  for (size_t i = 0; i < self->n_data_columns; i++) {
    BP_WORKER_ASSERT(&self->base, self->base.sinks[i] != NULL, Bp_EC_NO_SINK);
  }

  // Validate all sinks have the same batch capacity
  if (self->n_data_columns > 1) {
    uint8_t expected_capacity_expo = self->base.sinks[0]->batch_capacity_expo;
    for (size_t i = 1; i < self->n_data_columns; i++) {
      BP_WORKER_ASSERT(
          &self->base,
          self->base.sinks[i]->batch_capacity_expo == expected_capacity_expo,
          Bp_EC_INVALID_CONFIG);
    }
  }

  double* value_buffer = malloc(self->n_data_columns * sizeof(double));
  BP_WORKER_ASSERT(&self->base, value_buffer != NULL, Bp_EC_MALLOC_FAIL);

  BatchState state = {0};

  while (atomic_load(&self->base.running)) {
    if (!fgets(self->line_buffer, self->line_buffer_size, self->file)) {
      if (feof(self->file)) {
        if (self->loop) {
          fseek(self->file, 0, SEEK_SET);
          if (self->has_header) {
            fgets(self->line_buffer, self->line_buffer_size, self->file);
          }
          continue;
        } else {
          break;  // Exit loop to submit any remaining data
        }
      } else {
        free(value_buffer);
        BP_WORKER_ASSERT(&self->base, false, Bp_EC_INVALID_DATA);
      }
    }

    // Check if line was truncated (no newline and not EOF)
    size_t len = strlen(self->line_buffer);
    if (len > 0 && self->line_buffer[len - 1] != '\n' && !feof(self->file)) {
      free(value_buffer);
      BP_WORKER_ASSERT(&self->base, false, Bp_EC_INVALID_DATA);
    }

    self->current_line++;

    uint64_t timestamp;
    Bp_EC err = parse_line(self, self->line_buffer, &timestamp, value_buffer);

    if (err != Bp_EC_OK) {
      if (self->skip_invalid) {
        continue;
      } else {
        free(value_buffer);
        BP_WORKER_ASSERT(&self->base, false, err);
      }
    }

    // Check if we need new batches before writing this sample
    if (need_new_batches(self, &state, timestamp)) {
      Bp_EC err = submit_and_get_new_batches(self, &state);
      if (err != Bp_EC_OK) {
        free(value_buffer);
        BP_WORKER_ASSERT(&self->base, false, err);
      }
    }

    // Write sample directly to all column batches
    write_sample_to_batches(self, &state, timestamp, value_buffer);
  }

  // Submit any remaining samples
  if (state.batches[0] && state.batches[0]->tail > 0) {
    uint64_t period_ns = state.delta_established ? state.expected_delta : 0;

    for (size_t col = 0; col < self->n_data_columns; col++) {
      Batch_t* batch = state.batches[col];
      batch->t_ns = state.batch_start_time;
      batch->period_ns = period_ns;
      batch->head = 0;  // Data starts at index 0
      // tail is already set to the number of samples
      batch->ec = Bp_EC_OK;
      bb_submit(self->base.sinks[col], self->base.timeout_us);
    }

    self->base.metrics.samples_processed += state.batches[0]->tail;
    self->base.metrics.n_batches++;
  }

  // Send completion batch to all outputs
  for (size_t col = 0; col < self->n_data_columns; col++) {
    Batch_t* completion_batch = bb_get_head(self->base.sinks[col]);
    if (completion_batch) {
      completion_batch->head = 0;
      completion_batch->tail = 0;
      completion_batch->ec = Bp_EC_COMPLETE;
      bb_submit(self->base.sinks[col], self->base.timeout_us);
    }
  }

  free(value_buffer);

  // Set error code if stopped cleanly (Bp_EC_OK is 0)
  if (self->base.worker_err_info.ec == Bp_EC_OK) {
    self->base.worker_err_info.ec = Bp_EC_STOPPED;
  }

  return NULL;
}

void csvsource_destroy(CsvSource_t* self)
{
  if (!self) return;

  if (self->file) {
    fclose(self->file);
    self->file = NULL;
  }

  if (self->file_path) {
    free(self->file_path);
    self->file_path = NULL;
  }

  if (self->line_buffer) {
    free(self->line_buffer);
    self->line_buffer = NULL;
  }

  // No longer need to free timestamp_buffer or data_accumulation_buffer

  if (self->header_names) {
    for (size_t i = 0; i < self->n_header_columns; i++) {
      if (self->header_names[i]) {
        free(self->header_names[i]);
      }
    }
    free(self->header_names);
    self->header_names = NULL;
  }

  filt_deinit(&self->base);
}

static Bp_EC csvsource_describe(Filter_t* self, char* buffer, size_t size)
{
  CsvSource_t* source = (CsvSource_t*) self;
  int written = 0;

  written +=
      snprintf(buffer + written, size - written, "CsvSource: %s\n", self->name);
  written += snprintf(buffer + written, size - written, "  File: %s\n",
                      source->file_path);
  written += snprintf(buffer + written, size - written, "  Delimiter: '%c'\n",
                      source->delimiter);
  written += snprintf(buffer + written, size - written, "  Has header: %s\n",
                      source->has_header ? "yes" : "no");
  written += snprintf(buffer + written, size - written,
                      "  Timestamp column: %s\n", source->ts_column_name);
  written += snprintf(buffer + written, size - written, "  Data columns: ");

  for (size_t i = 0; i < source->n_data_columns; i++) {
    written += snprintf(buffer + written, size - written, "%s%s",
                        source->data_column_names[i],
                        i < source->n_data_columns - 1 ? ", " : "\n");
  }

  written +=
      snprintf(buffer + written, size - written, "  Regular timing: %s\n",
               source->detect_regular_timing ? "enabled" : "disabled");
  if (source->is_regular) {
    written +=
        snprintf(buffer + written, size - written,
                 "  Detected period: %lu ns\n", source->detected_period_ns);
  }
  written += snprintf(buffer + written, size - written, "  Loop mode: %s\n",
                      source->loop ? "enabled" : "disabled");
  written += snprintf(buffer + written, size - written, "  Skip invalid: %s\n",
                      source->skip_invalid ? "yes" : "no");

  return Bp_EC_OK;
}

static Bp_EC csvsource_get_stats(Filter_t* self, void* stats_out)
{
  // Copy current metrics
  Filt_metrics* stats = (Filt_metrics*) stats_out;
  *stats = self->metrics;
  return Bp_EC_OK;
}
