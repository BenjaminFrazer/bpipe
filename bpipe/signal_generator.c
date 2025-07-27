#define _GNU_SOURCE  // For usleep
#include "signal_generator.h"
#include <math.h>
#include <stdatomic.h>
#include <stddef.h>
#include <unistd.h>
#include "batch_buffer.h"
#include "bperr.h"
#include "core.h"
#include "utils.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Generate sine waveform
static void generate_sine(SignalGenerator_t* sg, float* samples, size_t n,
                          uint64_t t_start_ns)
{
  for (size_t i = 0; i < n; i++) {
    double t_ns = t_start_ns + i * sg->period_ns;
    double phase = sg->omega * t_ns + sg->initial_phase_rad;
    samples[i] = sg->amplitude * sin(phase) + sg->offset;
  }
}

// Generate square waveform
static void generate_square(SignalGenerator_t* sg, float* samples, size_t n,
                            uint64_t t_start_ns)
{
  for (size_t i = 0; i < n; i++) {
    double t_ns = t_start_ns + i * sg->period_ns;
    double phase = sg->omega * t_ns + sg->initial_phase_rad;
    samples[i] = sg->amplitude * (sin(phase) >= 0 ? 1.0 : -1.0) + sg->offset;
  }
}

// Generate sawtooth waveform
static void generate_sawtooth(SignalGenerator_t* sg, float* samples, size_t n,
                              uint64_t t_start_ns)
{
  for (size_t i = 0; i < n; i++) {
    double t_ns = t_start_ns + i * sg->period_ns;
    double phase = sg->omega * t_ns + sg->initial_phase_rad;
    // Normalize phase to [0, 2π]
    double normalized = fmod(phase, 2.0 * M_PI);
    if (normalized < 0) normalized += 2.0 * M_PI;
    // Convert to sawtooth: -1 to +1
    samples[i] =
        sg->amplitude * (2.0 * normalized / (2.0 * M_PI) - 1.0) + sg->offset;
  }
}

// Generate triangle waveform
static void generate_triangle(SignalGenerator_t* sg, float* samples, size_t n,
                              uint64_t t_start_ns)
{
  for (size_t i = 0; i < n; i++) {
    double t_ns = t_start_ns + i * sg->period_ns;
    double phase = sg->omega * t_ns + sg->initial_phase_rad;
    // Normalize phase to [0, 2π]
    double normalized = fmod(phase, 2.0 * M_PI);
    if (normalized < 0) normalized += 2.0 * M_PI;
    // Convert to triangle
    double value;
    if (normalized < M_PI) {
      value = 2.0 * normalized / M_PI - 1.0;
    } else {
      value = 3.0 - 2.0 * normalized / M_PI;
    }
    samples[i] = sg->amplitude * value + sg->offset;
  }
}

// Generate waveform based on type
static void generate_waveform(SignalGenerator_t* sg, float* samples, size_t n,
                              uint64_t t_start_ns)
{
  switch (sg->waveform_type) {
    case WAVEFORM_SINE:
      generate_sine(sg, samples, n, t_start_ns);
      break;
    case WAVEFORM_SQUARE:
      generate_square(sg, samples, n, t_start_ns);
      break;
    case WAVEFORM_SAWTOOTH:
      generate_sawtooth(sg, samples, n, t_start_ns);
      break;
    case WAVEFORM_TRIANGLE:
      generate_triangle(sg, samples, n, t_start_ns);
      break;
  }
}

// Send completion signal to all connected sinks
static void send_completion_to_sinks(Filter_t* filter)
{
  for (int i = 0; i < filter->n_sinks; i++) {
    if (filter->sinks[i] != NULL) {
      Batch_t* batch = bb_get_head(filter->sinks[i]);
      if (batch) {
        batch->ec = Bp_EC_COMPLETE;
        batch->tail = 0;
        batch->head = 0;
        bb_submit(filter->sinks[i], 0);  // No timeout for completion
      }
    }
  }
}

// Worker thread function
void* signal_generator_worker(void* arg)
{
  SignalGenerator_t* sg = (SignalGenerator_t*) arg;
  Bp_EC err = Bp_EC_OK;

  // Validate configuration
  BP_WORKER_ASSERT(&sg->base, sg->base.n_sinks > 0, Bp_EC_NO_SINK);
  BP_WORKER_ASSERT(&sg->base, sg->base.sinks[0] != NULL, Bp_EC_NO_SINK);

  // Check Nyquist frequency if configured
  if (!sg->allow_aliasing) {
    double nyquist_hz = 0.5e9 / sg->period_ns;
    BP_WORKER_ASSERT(&sg->base, sg->frequency_hz <= nyquist_hz,
                     Bp_EC_INVALID_CONFIG);
  }

  // Initialize timing
  sg->next_t_ns = sg->start_time_ns;

  while (atomic_load(&sg->base.running)) {
    // Get output batch
    Batch_t* output = bb_get_head(sg->base.sinks[0]);
    if (!output) {
      // Buffer full, wait and retry
      usleep(1000);
      continue;
    }

    // Calculate samples to generate
    size_t n_samples = bb_batch_size(sg->base.sinks[0]);
    if (sg->max_samples) {
      n_samples = MIN(n_samples, sg->max_samples - sg->samples_generated);
    }

    // Set batch metadata
    output->t_ns = sg->next_t_ns;
    output->period_ns = sg->period_ns;
    output->tail = 0;
    output->head = n_samples;
    output->ec = Bp_EC_OK;

    // Generate waveform
    float* samples = (float*) output->data;
    generate_waveform(sg, samples, n_samples, sg->next_t_ns);

    // Update state
    sg->next_t_ns += n_samples * sg->period_ns;
    sg->samples_generated += n_samples;

    // Submit batch
    err = bb_submit(sg->base.sinks[0], sg->base.timeout_us);
    if (err != Bp_EC_OK) {
      BP_WORKER_ASSERT(&sg->base, false, err);
    }

    // Update metrics
    sg->base.metrics.samples_processed += n_samples;
    sg->base.metrics.n_batches++;

    // Check termination after generating samples
    if (sg->max_samples && sg->samples_generated >= sg->max_samples) {
      atomic_store(&sg->base.running, false);
      break;
    }
  }

  // Send completion
  send_completion_to_sinks(&sg->base);

  return NULL;
}

// Initialize signal generator
Bp_EC signal_generator_init(SignalGenerator_t* sg,
                            SignalGenerator_config_t config)
{
  if (sg == NULL) {
    return Bp_EC_NULL_FILTER;
  }

  // Validate configuration
  if (config.frequency_hz <= 0) {
    return Bp_EC_INVALID_CONFIG;
  }
  if (config.sample_period_ns == 0) {
    return Bp_EC_INVALID_CONFIG;
  }
  if (config.waveform_type < WAVEFORM_SINE ||
      config.waveform_type > WAVEFORM_TRIANGLE) {
    return Bp_EC_INVALID_CONFIG;
  }

  // Build core config
  Core_filt_config_t core_config = {
      .name = config.name,
      .filt_type = FILT_T_MAP,  // Using MAP type for source filter
      .size = sizeof(SignalGenerator_t),
      .n_inputs = 0,  // No inputs
      .max_supported_sinks = MAX_SINKS,
      .buff_config = config.buff_config,
      .timeout_us = config.timeout_us,
      .worker = signal_generator_worker};

  // Initialize base filter
  Bp_EC err = filt_init(&sg->base, core_config);
  if (err != Bp_EC_OK) {
    return err;
  }

  // Cache configuration
  sg->waveform_type = config.waveform_type;
  sg->frequency_hz = config.frequency_hz;
  sg->omega = 2.0 * M_PI * config.frequency_hz * 1e-9;  // Pre-compute rad/ns
  sg->initial_phase_rad = config.phase_rad;
  sg->amplitude = config.amplitude;
  sg->offset = config.offset;
  sg->period_ns = config.sample_period_ns;
  sg->max_samples = config.max_samples;
  sg->allow_aliasing = config.allow_aliasing;
  sg->start_time_ns = config.start_time_ns;

  // Initialize runtime state
  sg->next_t_ns = 0;
  sg->samples_generated = 0;

  return Bp_EC_OK;
}