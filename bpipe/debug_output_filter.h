#ifndef BPIPE_DEBUG_OUTPUT_FILTER_H
#define BPIPE_DEBUG_OUTPUT_FILTER_H

#include <stdio.h>
#include "core.h"

typedef enum {
  DEBUG_FMT_DECIMAL,
  DEBUG_FMT_HEX,
  DEBUG_FMT_SCIENTIFIC,
  DEBUG_FMT_BINARY
} DebugOutputFormat;

typedef struct {
  const char* prefix;
  bool show_metadata;
  bool show_samples;
  int max_samples_per_batch;
  DebugOutputFormat format;
  bool flush_after_print;
  const char* filename;
  bool append_mode;
} DebugOutputConfig_t;

typedef struct {
  Filter_t base;
  DebugOutputConfig_t config;
  char* formatted_prefix;
  FILE* output_file;
  pthread_mutex_t file_mutex;
} DebugOutputFilter_t;

Bp_EC debug_output_filter_init(DebugOutputFilter_t* filter,
                               const DebugOutputConfig_t* config);

#endif  // BPIPE_DEBUG_OUTPUT_FILTER_H