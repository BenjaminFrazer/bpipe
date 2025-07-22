
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
