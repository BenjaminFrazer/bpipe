#define _GNU_SOURCE  // For usleep
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bpipe/core.h"
#include "bpipe/csv_source.h"
#include "bpipe/debug_output_filter.h"
#include "bpipe/utils.h"

int main(int argc, char* argv[])
{
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <csv_file> [output_file1] [output_file2] ...\n",
            argv[0]);
    fprintf(stderr, "If no output files specified, prints to stdout\n");
    fprintf(stderr,
            "Extra columns beyond number of output files will be discarded\n");
    return 1;
  }

  const char* csv_file = argv[1];
  int n_output_files = argc - 2;  // Number of output files specified

  // Initialize CSV source filter
  CsvSource_t csv_source;
  CsvSource_config_t csv_config = {
      .name = "csv_reader",
      .file_path = csv_file,
      .delimiter = ',',
      .has_header = true,
      .ts_column_name = "timestamp",
      .data_column_names = {"value1", NULL},  // Read two data columns
      .detect_regular_timing = true,
      .regular_threshold_ns = 1000,
      .batch_size = 64,
      .ring_capacity = 256,
      .output_dtype = DTYPE_FLOAT,
      .timeout_us = 100000,
      .loop = false,
      .skip_invalid = false};

  Bp_EC err = csvsource_init(&csv_source, csv_config);
  if (err != Bp_EC_OK) {
    fprintf(stderr, "Failed to initialize CSV source: %d\n", err);
    return 1;
  }

  // Determine number of debug filters to create (max 2 columns from CSV)
  int n_debug_filters = (n_output_files > 0) ? MIN(n_output_files, 2) : 2;

  // Initialize debug output filters
  DebugOutputFilter_t* debug_filters =
      calloc(n_debug_filters, sizeof(DebugOutputFilter_t));
  if (!debug_filters) {
    fprintf(stderr, "Failed to allocate memory for debug filters\n");
    csvsource_destroy(&csv_source);
    return 1;
  }

  // Create debug filters
  for (int i = 0; i < n_debug_filters; i++) {
    DebugOutputConfig_t debug_config = {
        .prefix = (i == 0) ? "[Column 1] " : "[Column 2] ",
        .show_metadata = true,
        .show_samples = true,
        .max_samples_per_batch = 10,
        .format = DEBUG_FMT_DECIMAL,
        .filename = (i < n_output_files) ? argv[i + 2] : NULL,
        .append_mode = false,
        .flush_after_print = true};

    err = debug_output_filter_init(&debug_filters[i], &debug_config);
    if (err != Bp_EC_OK) {
      fprintf(stderr, "Failed to initialize debug output filter %d: %d\n",
              i + 1, err);
      // Clean up previously initialized filters
      for (int j = 0; j < i; j++) {
        filt_deinit(&debug_filters[j].base);
      }
      free(debug_filters);
      csvsource_destroy(&csv_source);
      return 1;
    }
  }

  // Connect filters: CSV source -> Debug output filters
  for (int i = 0; i < n_debug_filters; i++) {
    err = filt_sink_connect(&csv_source.base, i,
                            &debug_filters[i].base.input_buffers[0]);
    if (err != Bp_EC_OK) {
      fprintf(stderr,
              "Failed to connect CSV source output %d to debug filter: %d\n", i,
              err);
      // Clean up
      for (int j = 0; j < n_debug_filters; j++) {
        filt_deinit(&debug_filters[j].base);
      }
      free(debug_filters);
      csvsource_destroy(&csv_source);
      return 1;
    }
  }

  // Debug output filters don't need sink buffers - they write directly to
  // files/stdout

  // Start all filters
  printf("Starting CSV to debug output pipeline...\n");
  printf("Reading from: %s\n", csv_file);
  printf("Expected columns: timestamp, value1, value2\n");
  if (n_output_files > 0) {
    printf("Writing %d columns to output files\n", n_debug_filters);
  } else {
    printf("Writing to stdout\n");
  }
  if (n_debug_filters < 2) {
    printf("Note: Column %d will be discarded\n", n_debug_filters + 1);
  }
  printf("\n");

  err = filt_start(&csv_source.base);
  if (err != Bp_EC_OK) {
    fprintf(stderr, "Failed to start CSV source: %d\n", err);
    for (int i = 0; i < n_debug_filters; i++) {
      filt_deinit(&debug_filters[i].base);
    }
    free(debug_filters);
    csvsource_destroy(&csv_source);
    return 1;
  }

  // Start debug filters
  for (int i = 0; i < n_debug_filters; i++) {
    err = filt_start(&debug_filters[i].base);
    if (err != Bp_EC_OK) {
      fprintf(stderr, "Failed to start debug filter %d: %d\n", i + 1, err);
      // Stop already started filters
      for (int j = 0; j < i; j++) {
        filt_stop(&debug_filters[j].base);
      }
      filt_stop(&csv_source.base);
      // Clean up
      for (int j = 0; j < n_debug_filters; j++) {
        filt_deinit(&debug_filters[j].base);
      }
      free(debug_filters);
      csvsource_destroy(&csv_source);
      return 1;
    }
  }

  // Wait for pipeline to complete
  // The CSV source will stop when EOF is reached and send completion signals
  // Give filters time to process all data
  usleep(1000000);  // Sleep for 1 second to let pipeline complete

  // Stop filters (this will also join their threads)
  printf("\nStopping pipeline...\n");
  filt_stop(&csv_source.base);
  for (int i = 0; i < n_debug_filters; i++) {
    filt_stop(&debug_filters[i].base);
  }

  // Clean up
  for (int i = 0; i < n_debug_filters; i++) {
    filt_deinit(&debug_filters[i].base);
  }
  free(debug_filters);
  csvsource_destroy(&csv_source);

  printf("Pipeline completed successfully.\n");
  return 0;
}
