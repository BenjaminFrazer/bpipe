#define _GNU_SOURCE  // For strdup
#include "csv_sink.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Define our error codes since they're not in bperr.h yet
#define Bp_EC_FILE_ERROR Bp_EC_ALLOC             // Reuse existing error
#define Bp_EC_FILE_FULL Bp_EC_NO_SPACE           // Reuse existing error
#define Bp_EC_PERMISSION_DENIED Bp_EC_ALLOC      // Reuse existing error
#define Bp_EC_DISK_FULL Bp_EC_NO_SPACE           // Reuse existing error
#define Bp_EC_FILE_NOT_FOUND Bp_EC_ALLOC         // Reuse existing error
#define Bp_EC_NULL_PTR Bp_EC_NULL_POINTER        // Use existing error
#define Bp_EC_UNSUPPORTED_TYPE Bp_EC_TYPE_ERROR  // Use existing error

#define MAX_LINE_LENGTH 4096

// Forward declarations
static void* csv_sink_worker(void* arg);
static Bp_EC open_output_file(CSVSink_t* sink);
static void close_output_file(CSVSink_t* sink);
static void write_csv_header(CSVSink_t* sink);
static void format_csv_line(CSVSink_t* sink, uint64_t t_ns, void* data);
static Bp_EC csv_sink_describe(Filter_t* self, char* buffer, size_t size);

// Initialize CSV sink filter
Bp_EC csv_sink_init(CSVSink_t* sink, CSVSink_config_t config)
{
  if (sink == NULL) return Bp_EC_NULL_PTR;
  if (config.output_path == NULL) return Bp_EC_INVALID_CONFIG;

  // Validate configuration
  if (config.format == CSV_FORMAT_MULTI_COL && config.n_columns == 0) {
    return Bp_EC_INVALID_CONFIG;
  }

  // Build core filter config
  Core_filt_config_t core_config = {
      .name = config.name,
      .filt_type = FILT_T_MAP,  // Using MAP type for sink
      .size = sizeof(CSVSink_t),
      .n_inputs = 1,             // Single input
      .max_supported_sinks = 0,  // No outputs (sink filter)
      .buff_config = config.buff_config,
      .timeout_us = 1000000,  // 1 second timeout
      .worker = csv_sink_worker};

  // Initialize base filter
  Bp_EC err = filt_init(&sink->base, core_config);
  if (err != Bp_EC_OK) return err;

  // Cache configuration
  sink->format = config.format;
  strncpy(sink->delimiter, config.delimiter ? config.delimiter : ",", 1);
  sink->delimiter[1] = '\0';
  sink->line_ending = config.line_ending ? config.line_ending : "\n";
  sink->precision = config.precision > 0 ? config.precision : 6;
  sink->max_file_size_bytes = config.max_file_size_bytes;
  sink->column_names = config.column_names;
  sink->n_columns = config.n_columns;
  sink->write_header = config.write_header;

  // Allocate filename buffer
  sink->current_filename = strdup(config.output_path);
  if (!sink->current_filename) {
    return Bp_EC_ALLOC;
  }

  // Store file open parameters
  sink->file = NULL;
  sink->bytes_written = 0;
  sink->lines_written = 0;
  sink->samples_written = 0;
  sink->batches_processed = 0;

  // Validate file access during init
  err = open_output_file(sink);
  if (err != Bp_EC_OK) {
    free(sink->current_filename);
    return err;
  }
  close_output_file(sink);

  // Override describe operation
  sink->base.ops.describe = csv_sink_describe;

  return Bp_EC_OK;
}

// Worker thread function
static void* csv_sink_worker(void* arg)
{
  CSVSink_t* sink = (CSVSink_t*) arg;
  Bp_EC err = Bp_EC_OK;

  // Validate sink has no outputs
  BP_WORKER_ASSERT(&sink->base, sink->base.n_sinks == 0, Bp_EC_INVALID_CONFIG);

  // Re-open output file (already validated in init)
  err = open_output_file(sink);
  BP_WORKER_ASSERT(&sink->base, err == Bp_EC_OK, err);

  // Write header if configured
  if (sink->write_header) {
    write_csv_header(sink);
  }

  while (atomic_load(&sink->base.running)) {
    // Get input batch
    Batch_t* input =
        bb_get_tail(&sink->base.input_buffers[0], sink->base.timeout_us, &err);
    if (!input) {
      if (err == Bp_EC_TIMEOUT) continue;
      if (err == Bp_EC_STOPPED) break;
      break;  // Real error
    }

    // Check for completion
    if (input->ec == Bp_EC_COMPLETE) {
      bb_del_tail(&sink->base.input_buffers[0]);
      atomic_store(&sink->base.running, false);  // Stop the filter
      break;
    }

    // Validate input
    BP_WORKER_ASSERT(&sink->base, input->ec == Bp_EC_OK, input->ec);

    // Get data type info
    size_t data_width = bb_getdatawidth(sink->base.input_buffers[0].dtype);
    BP_WORKER_ASSERT(&sink->base, data_width > 0, Bp_EC_UNSUPPORTED_TYPE);

    // Process batch data
    size_t samples = input->tail - input->head;
    for (size_t i = 0; i < samples; i++) {
      // Calculate timestamp for this sample
      uint64_t sample_time_ns = input->t_ns + i * input->period_ns;

      // Calculate data pointer
      char* data_ptr = ((char*) input->data) + (input->head + i) * data_width;

      // Format and write the CSV line
      format_csv_line(sink, sample_time_ns, data_ptr);

      // Check file size limit
      if (sink->max_file_size_bytes > 0 &&
          sink->bytes_written >= sink->max_file_size_bytes) {
        bb_del_tail(&sink->base.input_buffers[0]);
        BP_WORKER_ASSERT(&sink->base, false, Bp_EC_FILE_FULL);
      }
    }

    // Flush after each batch for data integrity
    fflush(sink->file);

    // Update metrics
    sink->samples_written += samples;
    sink->batches_processed++;
    sink->base.metrics.samples_processed += samples;
    sink->base.metrics.n_batches++;

    // Release input batch
    bb_del_tail(&sink->base.input_buffers[0]);
  }

  // Close output file
  close_output_file(sink);

  return NULL;
}

// Open output file
static Bp_EC open_output_file(CSVSink_t* sink)
{
  const char* mode =
      sink->write_header ? "w" : "a";  // Overwrite if writing header

  sink->file = fopen(sink->current_filename, mode);
  if (!sink->file) {
    // Provide specific error message based on errno
    if (errno == EACCES) {
      return Bp_EC_PERMISSION_DENIED;
    } else if (errno == ENOSPC) {
      return Bp_EC_DISK_FULL;
    } else if (errno == ENOENT) {
      return Bp_EC_FILE_NOT_FOUND;
    }
    return Bp_EC_FILE_ERROR;
  }

  // Set file permissions if creating new file
  if (sink->write_header) {
    chmod(sink->current_filename, 0644);
  }

  return Bp_EC_OK;
}

// Close output file
static void close_output_file(CSVSink_t* sink)
{
  if (sink->file) {
    fflush(sink->file);
    fclose(sink->file);
    sink->file = NULL;
  }
}

// Write CSV header
static void write_csv_header(CSVSink_t* sink)
{
  if (!sink->file) return;

  // Write timestamp column
  fprintf(sink->file, "timestamp_ns");

  if (sink->format == CSV_FORMAT_SIMPLE) {
    // Single value column
    fprintf(sink->file, "%svalue", sink->delimiter);
  } else if (sink->format == CSV_FORMAT_MULTI_COL) {
    // Multiple columns
    for (size_t i = 0; i < sink->n_columns; i++) {
      fprintf(sink->file, "%s", sink->delimiter);
      if (sink->column_names && sink->column_names[i]) {
        fprintf(sink->file, "%s", sink->column_names[i]);
      } else {
        fprintf(sink->file, "channel_%zu", i);
      }
    }
  }

  fprintf(sink->file, "%s", sink->line_ending);
  sink->lines_written++;
  sink->bytes_written = ftell(sink->file);
}

// Format and write CSV line
static void format_csv_line(CSVSink_t* sink, uint64_t t_ns, void* data)
{
  char line[MAX_LINE_LENGTH];
  size_t len = 0;

  // Format timestamp (nanoseconds)
  len += snprintf(line + len, sizeof(line) - len, "%llu",
                  (unsigned long long) t_ns);

  // Add delimiter
  line[len++] = sink->delimiter[0];

  // Format data value(s)
  SampleDtype_t dtype = sink->base.input_buffers[0].dtype;

  if (sink->format == CSV_FORMAT_SIMPLE) {
    // Single value
    switch (dtype) {
      case DTYPE_FLOAT:
        len += snprintf(line + len, sizeof(line) - len, "%.*f", sink->precision,
                        *(float*) data);
        break;
      case DTYPE_I32:
        len += snprintf(line + len, sizeof(line) - len, "%d", *(int32_t*) data);
        break;
      case DTYPE_U32:
        len +=
            snprintf(line + len, sizeof(line) - len, "%u", *(uint32_t*) data);
        break;
      default:
        // Should not reach here due to validation
        return;
    }
  } else if (sink->format == CSV_FORMAT_MULTI_COL) {
    // Multiple values - assume data is array
    size_t data_width = bb_getdatawidth(dtype);
    for (size_t i = 0; i < sink->n_columns; i++) {
      if (i > 0) {
        line[len++] = sink->delimiter[0];
      }

      void* element = ((char*) data) + i * data_width;
      switch (dtype) {
        case DTYPE_FLOAT:
          len += snprintf(line + len, sizeof(line) - len, "%.*f",
                          sink->precision, *(float*) element);
          break;
        case DTYPE_I32:
          len += snprintf(line + len, sizeof(line) - len, "%d",
                          *(int32_t*) element);
          break;
        case DTYPE_U32:
          len += snprintf(line + len, sizeof(line) - len, "%u",
                          *(uint32_t*) element);
          break;
        default:
          return;
      }
    }
  }

  // Add line ending
  strcpy(line + len, sink->line_ending);
  len += strlen(sink->line_ending);

  // Write the line directly
  fwrite(line, 1, len, sink->file);
  sink->bytes_written += len;
  sink->lines_written++;
}

// Describe operation
static Bp_EC csv_sink_describe(Filter_t* self, char* buffer, size_t size)
{
  CSVSink_t* sink = (CSVSink_t*) self;
  snprintf(buffer, size,
           "CSVSink: %s\n"
           "  Output file: %s\n"
           "  Format: %s\n"
           "  Lines written: %llu\n"
           "  Samples written: %llu\n"
           "  File size: %zu bytes\n",
           self->name, sink->current_filename,
           sink->format == CSV_FORMAT_SIMPLE ? "Simple" : "Multi-column",
           (unsigned long long) sink->lines_written,
           (unsigned long long) sink->samples_written, sink->bytes_written);
  return Bp_EC_OK;
}