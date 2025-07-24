#ifndef CSV_SINK_H
#define CSV_SINK_H

#include <stdio.h>
#include "core.h"
#include "utils.h"

// CSV output format
typedef enum {
  CSV_FORMAT_SIMPLE,     // timestamp,value
  CSV_FORMAT_MULTI_COL,  // timestamp,ch0,ch1,ch2...
} CSVFormat_e;

// Configuration structure
typedef struct _CSVSink_config_t {
  const char* name;
  BatchBuffer_config buff_config;  // Input buffer config

  // File configuration
  const char* output_path;     // File path
  bool append;                 // Append vs overwrite
  mode_t file_mode;            // Unix file permissions (0644)
  size_t max_file_size_bytes;  // Maximum file size (0 = unlimited)

  // CSV format
  CSVFormat_e format;         // Output format (SIMPLE or MULTI_COL)
  const char* delimiter;      // Field delimiter (default ",")
  const char* line_ending;    // Line ending (default "\n")
  bool write_header;          // Write column headers
  const char** column_names;  // Custom column names (optional)
  size_t n_columns;           // Number of columns for MULTI_COL
  int precision;              // Decimal places for floats
} CSVSink_config_t;

// Filter structure
typedef struct _CSVSink_t {
  Filter_t base;  // MUST be first member

  // Configuration (cached)
  CSVFormat_e format;
  char delimiter[2];  // Single char + null terminator
  const char* line_ending;
  int precision;
  size_t max_file_size_bytes;
  const char** column_names;
  size_t n_columns;
  bool write_header;

  // File management
  FILE* file;
  char* current_filename;
  size_t bytes_written;
  uint64_t lines_written;

  // State tracking
  uint64_t samples_written;
  uint64_t batches_processed;
} CSVSink_t;

// Public API
Bp_EC csv_sink_init(CSVSink_t* sink, CSVSink_config_t config);

#endif  // CSV_SINK_H