#ifndef PASSTHROUGH_H_
#define PASSTHROUGH_H_

#include "core.h"

typedef struct _Passthrough_config_t {
  const char* name;
  BatchBuffer_config buff_config;
  long timeout_us;
} Passthrough_config_t;

typedef struct _Passthrough_t {
  Filter_t base;  // MUST be first member
} Passthrough_t;

Bp_EC passthrough_init(Passthrough_t* pt, Passthrough_config_t* config);

#endif  // PASSTHROUGH_H_