#include "bperr.h"
#include "core.h"

typedef Bp_EC(Map_fcn_t)(char*, char*, size_t);

typedef struct _Map_filt_t {
  Filter_t base;
  Map_fcn_t* map_fcn;
} Map_filt_t;

typedef struct _Map_filt_config_t {
  BatchBuffer_config buff_config;
  Map_fcn_t* map_fcn;
} Map_config_t;

Bp_EC map_init(Map_filt_t* f, Map_config_t config);
