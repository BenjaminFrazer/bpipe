#ifndef MAP_H
#define MAP_H

#include "bperr.h"
#include "core.h"
#include "utils.h"

typedef Bp_EC (*Map_fcn_t)(const void* in, void* out, size_t n_samples);

typedef struct _Map_filt_t {
  Filter_t base;
  Map_fcn_t map_fcn;

  // Internal state for tracking partial batch consumption
  size_t input_consumed;  // Number of samples consumed from current input batch
  long long input_t_ns;   // Timestamp of current input batch being processed
  unsigned input_period_ns;  // Period of current input batch
} Map_filt_t;

typedef struct _Map_filt_config_t {
  const char* name;
  BatchBuffer_config buff_config;
  Map_fcn_t map_fcn;
  long timeout_us;
} Map_config_t;

Bp_EC map_init(Map_filt_t* f, Map_config_t config);

/* Example map functions */
Bp_EC map_identity_f32(const void* in, void* out, size_t n_samples);
Bp_EC map_identity_memcpy(const void* in, void* out, size_t n_samples);

#endif /* MAP_H */
