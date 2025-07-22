#include <math.h>
#include <stddef.h>
#include "batch_buffer.h"
#include "bperr.h"
#include "core.h"
#include "utils.h"

typedef enum {
  /*is quantity integer rational or floating point.*/
  NUM_INTEGER,
  NUM_RATIONAL,
  NUM_FLOAT,
} NumericPrecision_t;

typedef struct {
  NumericPrecision_t type;
  union {
    long i;
    float f;
    struct {
      unsigned num;   /* Numerator */
      unsigned denom; /* Denominator */
    } r;
  } val;
} Numeric_t;

float numeric_2_float(Numeric_t cfg, Bp_EC* err)
{
  float val;
  *err = Bp_EC_OK;
  switch (cfg.type) {
    case NUM_INTEGER:
      val = (float) cfg.val.i;
    case NUM_RATIONAL:
      val = (float) cfg.val.r.num / (float) cfg.val.r.denom;
      break;
    case NUM_FLOAT:
      val = cfg.val.f;
      break;
    default:
      *err = Bp_EC_INVALID_PRECISION;
      val = NAN;
      break;
  }
  return val;
};

typedef struct _waveform_cfg {
  Numeric_t phase;
  Numeric_t amplitude;
  Numeric_t period;
} WaveformCfg_t;

typedef Bp_EC(WaveformFcn_t)(void* data, size_t n_samples,
                             long long start_ts_ns, unsigned sample_period,
                             WaveformCfg_t cfg);
WaveformFcn_t sin_float;
Bp_EC sin_float(void* data, size_t n_samples, long long start_ts_ns,
                unsigned sample_period, WaveformCfg_t cfg)
{
  Bp_EC err;
  long long ts_ns = start_ts_ns;
  float period = numeric_2_float(cfg.phase, &err);
  if (err != Bp_EC_OK) return err;
  float phase = numeric_2_float(cfg.phase, &err);
  if (err != Bp_EC_OK) return err;
  float amplitude = numeric_2_float(cfg.phase, &err);
  if (err != Bp_EC_OK) return err;

  float* sample = (float*) data;
  for (int i = 0; i < n_samples; i++) {
    float theta = (2 * PI * ts_ns) / (period * 1000000000) + phase;
    float val = sin(theta) * amplitude;
    sample[i] = val;
    ts_ns += sample_period;
  }
  return Bp_EC_OK;
};

typedef struct _Signal_Generator_t {
  Filter_t base;
  WaveformFcn_t* waveform_fcn;
  WaveformCfg_t cfg;
} Signal_Generator_t;

typedef struct _Signal_Generator_Cfg_t {
  const char* name;
  long timeout_us;
  WaveformFcn_t* waveform_fcn;
  WaveformCfg_t waveform_cfg;
} Signal_Generator_Cfg_t;

Bp_EC validate_WaveformCfg(WaveformCfg_t cfg) { return Bp_EC_NOT_IMPLEMENTED; };

void* signal_generator_worker(void* arg)
{
  Signal_Generator_t* sg = (Signal_Generator_t*) arg;
  Bp_EC err;
  Batch_t* head;

  BP_WORKER_ASSERT(&(sg->base), sg->base.n_sinks == 1, Bp_EC_NO_SINK);
  BP_WORKER_ASSERT(&(sg->base), sg->base.sinks[0] != NULL, Bp_EC_NULL_BUFF);

  head = bb_get_head(sg->base.sinks[0]);

  BP_WORKER_ASSERT(&(sg->base), head != NULL, Bp_EC_NO_SINK);

  size_t n_batch_samples = bb_batch_size(sg->base.sinks[0]);
  long long start_ts_ns = head->t_ns;
  unsigned int sample_period = head->period_ns;

  while (sg->base.running) {
    err = sg->waveform_fcn(head->data, n_batch_samples, start_ts_ns,
                           sample_period, sg->cfg);
    BP_WORKER_ASSERT(&(sg->base), err == Bp_EC_OK, err);
  }
  return NULL;
};

Bp_EC sig_gen_init(Signal_Generator_t* sg, Signal_Generator_Cfg_t cfg)
{
  Core_filt_config_t core_config;
  Bp_EC err;
  if (sg == NULL) {
    return Bp_EC_NULL_FILTER;
  }
  err = validate_WaveformCfg(cfg.waveform_cfg);
  if (err != Bp_EC_OK) {
    return err;
  }
  /* copy Batch Buffer config */
  core_config.worker = &signal_generator_worker;

  /* Map is always a 1->1 filter */
  core_config.n_inputs = 0;                       //
  core_config.max_supported_sinks = 1;            //
  core_config.filt_type = FILT_T_MAP;             //
  core_config.size = sizeof(Signal_Generator_t);  // Size for inheritance
  core_config.name = cfg.name;
  core_config.timeout_us = cfg.timeout_us;

  err = filt_init(&sg->base, core_config);
  if (err != Bp_EC_OK) {
    return err;
  }

  if (cfg.waveform_fcn == NULL) {
    return Bp_EC_NULL_POINTER;
  }

  sg->waveform_fcn = cfg.waveform_fcn;
  return Bp_EC_OK;
};
