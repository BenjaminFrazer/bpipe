#ifndef CSV_SOURCE_H
#define CSV_SOURCE_H

#include <stdio.h>
#include "core.h"

#define BP_CSV_MAX_COLUMNS 64

typedef struct _CsvSource_config_t {
  const char* name;
  const char* file_path;

  char delimiter;
  bool has_header;
  const char* ts_column_name;
  const char* data_column_names[BP_CSV_MAX_COLUMNS];

  bool detect_regular_timing;
  uint64_t regular_threshold_ns;

  SampleDtype_t output_dtype;
  size_t batch_size;
  size_t ring_capacity;

  bool loop;
  bool skip_invalid;
  long timeout_us;
} CsvSource_config_t;

typedef struct _CsvSource_t {
  Filter_t base;

  FILE* file;
  char* file_path;
  char* line_buffer;
  size_t line_buffer_size;

  int ts_column_index;
  int data_column_indices[BP_CSV_MAX_COLUMNS];
  size_t n_data_columns;
  char** header_names;
  size_t n_header_columns;

  double parse_buffer[BP_CSV_MAX_COLUMNS];
  size_t current_line;

  bool is_regular;
  uint64_t detected_period_ns;
  uint64_t last_timestamp_ns;

  uint64_t* timestamp_buffer;
  double* data_accumulation_buffer;  // Buffer for accumulating data samples
  size_t timestamps_in_buffer;

  char delimiter;
  bool has_header;
  const char* ts_column_name;
  const char* data_column_names[BP_CSV_MAX_COLUMNS];
  bool detect_regular_timing;
  uint64_t regular_threshold_ns;
  bool loop;
  bool skip_invalid;
  size_t batch_size;

} CsvSource_t;

Bp_EC csvsource_init(CsvSource_t* self, CsvSource_config_t config);
void csvsource_destroy(CsvSource_t* self);

#endif