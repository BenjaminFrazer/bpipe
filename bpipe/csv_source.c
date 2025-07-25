#define _GNU_SOURCE  // For strdup
#include "csv_source.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"

#define LINE_BUFFER_SIZE 4096

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

  if (!is_power_of_two(config.batch_size)) {
    return Bp_EC_INVALID_CONFIG;
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
  self->batch_size = config.batch_size;

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
    return Bp_EC_INVALID_CONFIG;
  }

  self->line_buffer_size = LINE_BUFFER_SIZE;
  self->line_buffer = malloc(self->line_buffer_size);
  if (!self->line_buffer) {
    fclose(self->file);
    free(self->file_path);
    return Bp_EC_MALLOC_FAIL;
  }

  self->timestamp_buffer = malloc(config.batch_size * sizeof(uint64_t));
  if (!self->timestamp_buffer) {
    free(self->line_buffer);
    fclose(self->file);
    return Bp_EC_MALLOC_FAIL;
  }

  self->data_accumulation_buffer =
      malloc(config.batch_size * self->n_data_columns * sizeof(double));
  if (!self->data_accumulation_buffer) {
    free(self->timestamp_buffer);
    free(self->line_buffer);
    fclose(self->file);
    return Bp_EC_MALLOC_FAIL;
  }

  if (self->has_header) {
    Bp_EC err = parse_header(self);
    if (err != Bp_EC_OK) {
      csvsource_destroy(self);
      return err;
    }
  }

  // Calculate capacity exponents from sizes
  size_t batch_capacity_expo = 0;
  size_t temp = config.batch_size;
  while (temp > 1) {
    batch_capacity_expo++;
    temp >>= 1;
  }

  size_t ring_capacity_expo = 0;
  temp = config.ring_capacity;
  while (temp > 1) {
    ring_capacity_expo++;
    temp >>= 1;
  }

  Core_filt_config_t filter_config = {
      .name = config.name,
      .filt_type = FILT_T_NDEF,  // Source filter
      .size = sizeof(CsvSource_t),
      .n_inputs = 0,
      .max_supported_sinks = MAX_SINKS,
      .buff_config = {.dtype = config.output_dtype,
                      .batch_capacity_expo = batch_capacity_expo,
                      .ring_capacity_expo = ring_capacity_expo,
                      .overflow_behaviour = OVERFLOW_BLOCK},
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
    return Bp_EC_INVALID_CONFIG;
  }

  for (size_t i = 0; i < self->n_data_columns; i++) {
    if (self->data_column_indices[i] == -1) {
      return Bp_EC_INVALID_CONFIG;
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

static void* csvsource_worker(void* arg)
{
  CsvSource_t* self = (CsvSource_t*) arg;
  Batch_buff_t* output_buffer = self->base.sinks[0];

  double* value_buffer = malloc(self->n_data_columns * sizeof(double));
  BP_WORKER_ASSERT(&self->base, value_buffer != NULL, Bp_EC_MALLOC_FAIL);

  self->timestamps_in_buffer = 0;
  bool first_sample = true;

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
          break;
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

    // Detect regularity at start or after a gap
    if (self->detect_regular_timing && self->timestamps_in_buffer == 0 &&
        !first_sample) {
      // Starting a new batch - could be regular
      self->is_regular = false;  // Will be set true if we detect regularity
    }

    if (first_sample) {
      self->last_timestamp_ns = timestamp;
      first_sample = false;
    } else if (self->detect_regular_timing && self->timestamps_in_buffer == 1) {
      // Calculate period from first two samples
      uint64_t period = timestamp - self->timestamp_buffer[0];
      if (!self->is_regular || self->detected_period_ns == 0) {
        // Either first time or re-detecting after gap
        self->detected_period_ns = period;
        self->is_regular = true;
      } else if (labs((int64_t) (period - self->detected_period_ns)) <=
                 self->regular_threshold_ns) {
        // Period matches previously detected period
        self->is_regular = true;
      } else {
        // Period doesn't match
        self->is_regular = false;
      }
    }

    if (self->is_regular && self->timestamps_in_buffer > 1) {
      uint64_t expected_ts =
          self->timestamp_buffer[0] +
          (self->timestamps_in_buffer * self->detected_period_ns);
      if (labs((int64_t) (timestamp - expected_ts)) >
          self->regular_threshold_ns) {
        self->is_regular = false;
      }
    }

    bool should_submit = false;

    if (self->timestamps_in_buffer >= self->batch_size) {
      should_submit = true;
    } else if (self->timestamps_in_buffer > 0) {
      // Check if we need to submit due to timing issues
      if (self->detect_regular_timing && self->detected_period_ns > 0) {
        // We have a detected period - check if current timestamp fits
        uint64_t expected_ts =
            self->last_timestamp_ns + self->detected_period_ns;
        if (labs((int64_t) (timestamp - expected_ts)) >
            self->regular_threshold_ns) {
          // Timing gap detected - submit current batch
          should_submit = true;
        }
      } else if (!self->detect_regular_timing) {
        // Not detecting regular timing - create single-sample batches
        should_submit = true;
      }
    }

    if (should_submit && self->timestamps_in_buffer > 0) {
      Batch_t* batch = bb_get_head(output_buffer);
      if (!batch) {
        free(value_buffer);
        BP_WORKER_ASSERT(&self->base, false, Bp_EC_TIMEOUT);
      }

      batch->head = 0;
      batch->tail = self->timestamps_in_buffer;
      batch->t_ns = self->timestamp_buffer[0];
      // For regular data, set period if we've detected it
      if (self->is_regular && self->detected_period_ns > 0) {
        batch->period_ns = self->detected_period_ns;
      } else if (self->detect_regular_timing &&
                 self->timestamps_in_buffer > 1) {
        // Check if this batch itself is regular
        uint64_t first_period =
            self->timestamp_buffer[1] - self->timestamp_buffer[0];
        bool batch_regular = true;
        for (size_t i = 2; i < self->timestamps_in_buffer; i++) {
          uint64_t period =
              self->timestamp_buffer[i] - self->timestamp_buffer[i - 1];
          if (labs((int64_t) (period - first_period)) >
              self->regular_threshold_ns) {
            batch_regular = false;
            break;
          }
        }
        batch->period_ns = batch_regular ? first_period : 0;
      } else {
        batch->period_ns = 0;
      }

      // Copy accumulated data to batch
      for (size_t s = 0; s < self->timestamps_in_buffer; s++) {
        for (size_t ch = 0; ch < self->n_data_columns; ch++) {
          size_t src_idx = s * self->n_data_columns + ch;
          size_t dst_idx = s * self->n_data_columns + ch;
          switch (output_buffer->dtype) {
            case DTYPE_FLOAT:
              ((float*) batch->data)[dst_idx] =
                  (float) self->data_accumulation_buffer[src_idx];
              break;
            case DTYPE_I32:
              ((int32_t*) batch->data)[dst_idx] =
                  (int32_t) self->data_accumulation_buffer[src_idx];
              break;
            case DTYPE_U32:
              ((uint32_t*) batch->data)[dst_idx] =
                  (uint32_t) self->data_accumulation_buffer[src_idx];
              break;
            default:
              break;
          }
        }
      }

      bb_submit(output_buffer, self->base.timeout_us);

      // Update metrics
      self->base.metrics.samples_processed += self->timestamps_in_buffer;
      self->base.metrics.n_batches++;

      self->timestamps_in_buffer = 0;
    }

    // Always store the current sample
    self->timestamp_buffer[self->timestamps_in_buffer] = timestamp;
    for (size_t ch = 0; ch < self->n_data_columns; ch++) {
      self->data_accumulation_buffer[self->timestamps_in_buffer *
                                         self->n_data_columns +
                                     ch] = value_buffer[ch];
    }
    self->timestamps_in_buffer++;
    self->last_timestamp_ns = timestamp;
  }

  if (self->timestamps_in_buffer > 0) {
    Batch_t* batch = bb_get_head(output_buffer);
    if (batch) {
      batch->head = 0;
      batch->tail = self->timestamps_in_buffer;
      batch->t_ns = self->timestamp_buffer[0];
      // For regular data, set period if we've detected it
      if (self->is_regular && self->detected_period_ns > 0) {
        batch->period_ns = self->detected_period_ns;
      } else if (self->detect_regular_timing &&
                 self->timestamps_in_buffer > 1) {
        // Check if this batch itself is regular
        uint64_t first_period =
            self->timestamp_buffer[1] - self->timestamp_buffer[0];
        bool batch_regular = true;
        for (size_t i = 2; i < self->timestamps_in_buffer; i++) {
          uint64_t period =
              self->timestamp_buffer[i] - self->timestamp_buffer[i - 1];
          if (labs((int64_t) (period - first_period)) >
              self->regular_threshold_ns) {
            batch_regular = false;
            break;
          }
        }
        batch->period_ns = batch_regular ? first_period : 0;
      } else {
        batch->period_ns = 0;
      }

      // Copy remaining data to batch
      for (size_t s = 0; s < self->timestamps_in_buffer; s++) {
        for (size_t ch = 0; ch < self->n_data_columns; ch++) {
          size_t src_idx = s * self->n_data_columns + ch;
          size_t dst_idx = s * self->n_data_columns + ch;
          switch (output_buffer->dtype) {
            case DTYPE_FLOAT:
              ((float*) batch->data)[dst_idx] =
                  (float) self->data_accumulation_buffer[src_idx];
              break;
            case DTYPE_I32:
              ((int32_t*) batch->data)[dst_idx] =
                  (int32_t) self->data_accumulation_buffer[src_idx];
              break;
            case DTYPE_U32:
              ((uint32_t*) batch->data)[dst_idx] =
                  (uint32_t) self->data_accumulation_buffer[src_idx];
              break;
            default:
              break;
          }
        }
      }

      bb_submit(output_buffer, self->base.timeout_us);

      // Update metrics
      self->base.metrics.samples_processed += self->timestamps_in_buffer;
      self->base.metrics.n_batches++;
    }
  }

  Batch_t* completion_batch = bb_get_head(output_buffer);
  if (completion_batch) {
    completion_batch->head = 0;
    completion_batch->tail = 0;
    completion_batch->ec = Bp_EC_COMPLETE;
    bb_submit(output_buffer, self->base.timeout_us);
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

  if (self->timestamp_buffer) {
    free(self->timestamp_buffer);
    self->timestamp_buffer = NULL;
  }

  if (self->data_accumulation_buffer) {
    free(self->data_accumulation_buffer);
    self->data_accumulation_buffer = NULL;
  }

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

  written += snprintf(buffer + written, size - written, "  Batch size: %zu\n",
                      source->batch_size);
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
