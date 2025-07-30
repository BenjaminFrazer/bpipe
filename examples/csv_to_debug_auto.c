#define _GNU_SOURCE  // For usleep, getline
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include "bpipe/core.h"
#include "bpipe/csv_source.h"
#include "bpipe/debug_output_filter.h"
#include "bpipe/utils.h"

#define MAX_COLUMN_NAME_LEN 256

// Helper function to parse CSV header and extract column names
int parse_csv_header(const char* filename, char delimiter, 
                     char*** column_names, int* n_columns,
                     int* timestamp_col_idx)
{
  FILE* file = fopen(filename, "r");
  if (!file) {
    fprintf(stderr, "Failed to open file: %s\n", filename);
    return -1;
  }

  char* line = NULL;
  size_t len = 0;
  ssize_t read = getline(&line, &len, file);
  fclose(file);
  
  if (read == -1) {
    fprintf(stderr, "Failed to read header from file: %s\n", filename);
    return -1;
  }

  // Remove newline if present
  if (line[read - 1] == '\n') {
    line[read - 1] = '\0';
  }

  // Count columns
  *n_columns = 1;
  for (int i = 0; line[i]; i++) {
    if (line[i] == delimiter) (*n_columns)++;
  }

  // Allocate column names array
  *column_names = calloc(*n_columns, sizeof(char*));
  if (!*column_names) {
    free(line);
    return -1;
  }

  // Parse column names
  char* token = strtok(line, &delimiter);
  int col_idx = 0;
  *timestamp_col_idx = -1;
  
  while (token && col_idx < *n_columns) {
    // Trim whitespace
    while (isspace(*token)) token++;
    char* end = token + strlen(token) - 1;
    while (end > token && isspace(*end)) *end-- = '\0';
    
    // Allocate and copy column name
    (*column_names)[col_idx] = strdup(token);
    
    // Check if this is a timestamp column
    if (strstr(token, "time") || strstr(token, "Time") || 
        strstr(token, "TIME") || strstr(token, "timestamp") ||
        strstr(token, "Timestamp") || strstr(token, "TIMESTAMP")) {
      *timestamp_col_idx = col_idx;
    }
    
    col_idx++;
    token = strtok(NULL, &delimiter);
  }

  free(line);
  return 0;
}

// Helper to create safe filename from column name
void make_safe_filename(const char* column_name, char* output, size_t output_size)
{
  snprintf(output, output_size, "%s.txt", column_name);
  
  // Replace problematic characters
  for (size_t i = 0; output[i] && i < output_size - 1; i++) {
    if (output[i] == '/' || output[i] == '\\' || output[i] == ':' ||
        output[i] == '*' || output[i] == '?' || output[i] == '"' ||
        output[i] == '<' || output[i] == '>' || output[i] == '|' ||
        output[i] == ' ') {
      output[i] = '_';
    }
  }
}

int main(int argc, char* argv[])
{
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <csv_file>\n", argv[0]);
    fprintf(stderr, "Automatically detects columns and creates output file for each\n");
    return 1;
  }

  const char* csv_file = argv[1];
  
  // Parse CSV header to get column names
  char** column_names = NULL;
  int n_columns = 0;
  int timestamp_col_idx = -1;
  
  if (parse_csv_header(csv_file, ',', &column_names, &n_columns, &timestamp_col_idx) != 0) {
    return 1;
  }
  
  printf("Detected %d columns in CSV file:\n", n_columns);
  for (int i = 0; i < n_columns; i++) {
    printf("  [%d] %s%s\n", i, column_names[i], 
           i == timestamp_col_idx ? " (timestamp)" : "");
  }
  printf("\n");
  
  if (timestamp_col_idx == -1) {
    fprintf(stderr, "Warning: No timestamp column detected. Using first column.\n");
    timestamp_col_idx = 0;
  }
  
  // Count data columns (excluding timestamp)
  int n_data_columns = 0;
  const char* data_column_names[BP_CSV_MAX_COLUMNS];
  
  for (int i = 0; i < n_columns && n_data_columns < BP_CSV_MAX_COLUMNS - 1; i++) {
    if (i != timestamp_col_idx) {
      data_column_names[n_data_columns] = column_names[i];
      n_data_columns++;
    }
  }
  data_column_names[n_data_columns] = NULL;  // NULL terminate
  
  printf("Processing %d data columns (excluding timestamp)\n", n_data_columns);

  // Initialize CSV source filter
  CsvSource_t csv_source;
  CsvSource_config_t csv_config = {
      .name = "csv_reader",
      .file_path = csv_file,
      .delimiter = ',',
      .has_header = true,
      .ts_column_name = column_names[timestamp_col_idx],
      .detect_regular_timing = true,
      .regular_threshold_ns = 1000,
      .timeout_us = 100000,
      .loop = false,
      .skip_invalid = false
  };
  
  // Copy data column names
  for (int i = 0; i < n_data_columns; i++) {
    csv_config.data_column_names[i] = data_column_names[i];
  }
  csv_config.data_column_names[n_data_columns] = NULL;

  Bp_EC err = csvsource_init(&csv_source, csv_config);
  if (err != Bp_EC_OK) {
    fprintf(stderr, "Failed to initialize CSV source: %d\n", err);
    for (int i = 0; i < n_columns; i++) free(column_names[i]);
    free(column_names);
    return 1;
  }

  // Initialize debug output filters - one per data column
  DebugOutputFilter_t* debug_filters = calloc(n_data_columns, sizeof(DebugOutputFilter_t));
  if (!debug_filters) {
    fprintf(stderr, "Failed to allocate memory for debug filters\n");
    csvsource_destroy(&csv_source);
    for (int i = 0; i < n_columns; i++) free(column_names[i]);
    free(column_names);
    return 1;
  }

  // Create debug filters with output files named after columns
  for (int i = 0; i < n_data_columns; i++) {
    char prefix[512];
    char filename[512];
    
    snprintf(prefix, sizeof(prefix), "[%s] ", data_column_names[i]);
    make_safe_filename(data_column_names[i], filename, sizeof(filename));
    
    printf("Creating output file: %s\n", filename);
    
    DebugOutputConfig_t debug_config = {
        .prefix = strdup(prefix),  // Need to allocate because config stores pointer
        .show_metadata = true,
        .show_samples = true,
        .max_samples_per_batch = 10,
        .format = DEBUG_FMT_DECIMAL,
        .filename = strdup(filename),  // Need to allocate
        .append_mode = false,
        .flush_after_print = true
    };

    err = debug_output_filter_init(&debug_filters[i], &debug_config);
    if (err != Bp_EC_OK) {
      fprintf(stderr, "Failed to initialize debug output filter %d: %d\n", i + 1, err);
      // Clean up previously initialized filters
      for (int j = 0; j < i; j++) {
        filt_deinit(&debug_filters[j].base);
      }
      free(debug_filters);
      csvsource_destroy(&csv_source);
      for (int j = 0; j < n_columns; j++) free(column_names[j]);
      free(column_names);
      return 1;
    }
  }

  // Connect filters: CSV source -> Debug output filters
  for (int i = 0; i < n_data_columns; i++) {
    err = filt_sink_connect(&csv_source.base, i, debug_filters[i].base.input_buffers[0]);
    if (err != Bp_EC_OK) {
      fprintf(stderr, "Failed to connect CSV source output %d to debug filter: %d\n", i, err);
      // Clean up
      for (int j = 0; j < n_data_columns; j++) {
        filt_deinit(&debug_filters[j].base);
      }
      free(debug_filters);
      csvsource_destroy(&csv_source);
      for (int j = 0; j < n_columns; j++) free(column_names[j]);
      free(column_names);
      return 1;
    }
  }

  // Start all filters
  printf("\nStarting CSV to debug output pipeline...\n");
  printf("Reading from: %s\n", csv_file);
  printf("Timestamp column: %s\n", column_names[timestamp_col_idx]);
  printf("Writing %d data columns to individual files\n\n", n_data_columns);

  err = filt_start(&csv_source.base);
  if (err != Bp_EC_OK) {
    fprintf(stderr, "Failed to start CSV source: %d\n", err);
    for (int i = 0; i < n_data_columns; i++) {
      filt_deinit(&debug_filters[i].base);
    }
    free(debug_filters);
    csvsource_destroy(&csv_source);
    for (int i = 0; i < n_columns; i++) free(column_names[i]);
    free(column_names);
    return 1;
  }

  // Start debug filters
  for (int i = 0; i < n_data_columns; i++) {
    err = filt_start(&debug_filters[i].base);
    if (err != Bp_EC_OK) {
      fprintf(stderr, "Failed to start debug filter %d: %d\n", i + 1, err);
      // Stop already started filters
      for (int j = 0; j < i; j++) {
        filt_stop(&debug_filters[j].base);
      }
      filt_stop(&csv_source.base);
      // Clean up
      for (int j = 0; j < n_data_columns; j++) {
        filt_deinit(&debug_filters[j].base);
      }
      free(debug_filters);
      csvsource_destroy(&csv_source);
      for (int j = 0; j < n_columns; j++) free(column_names[j]);
      free(column_names);
      return 1;
    }
  }

  // Wait for pipeline to complete
  usleep(1000000);  // Sleep for 1 second to let pipeline complete

  // Stop filters (this will also join their threads)
  printf("\nStopping pipeline...\n");
  filt_stop(&csv_source.base);
  for (int i = 0; i < n_data_columns; i++) {
    filt_stop(&debug_filters[i].base);
  }

  // Clean up
  for (int i = 0; i < n_data_columns; i++) {
    // Free the allocated strings
    free((void*)debug_filters[i].config.prefix);
    free((void*)debug_filters[i].config.filename);
    filt_deinit(&debug_filters[i].base);
  }
  free(debug_filters);
  csvsource_destroy(&csv_source);
  
  // Free column names
  for (int i = 0; i < n_columns; i++) {
    free(column_names[i]);
  }
  free(column_names);

  printf("Pipeline completed successfully.\n");
  printf("Output files created for each data column.\n");
  return 0;
}