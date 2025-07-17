#ifndef MAP_H
#define MAP_H

#include "bperr.h"
#include "core.h"

typedef Bp_EC (*Map_fcn_t)(const void* in, void* out, size_t n_samples);

typedef struct _Map_filt_t {
  Filter_t base;
  Map_fcn_t map_fcn;
} Map_filt_t;

typedef struct _Map_filt_config_t {
  BatchBuffer_config buff_config;
  Map_fcn_t map_fcn;
} Map_config_t;

Bp_EC map_init(Map_filt_t* f, Map_config_t config);

/* Example map functions */
Bp_EC map_identity_f32(const void* in, void* out, size_t n_samples);
Bp_EC map_identity_memcpy(const void* in, void* out, size_t n_samples);

#endif /* MAP_H */
