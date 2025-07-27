/**
 * test_signal_generator.c - Unit tests for the bpipe signal generator filter
 *
 * This test suite validates the signal generator implementation including:
 * - Basic initialization and configuration
 * - Waveform generation accuracy (sine, square, sawtooth, triangle)
 * - Phase continuity across batch boundaries
 * - Nyquist frequency validation
 * - Proper error handling and worker thread lifecycle
 *
 * The tests use a custom TestSink filter to capture generated samples
 * for validation. Each test verifies both functional correctness and
 * proper error handling through the CHECK_ERR macro.
 */

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../bpipe/batch_buffer.h"
#include "../bpipe/bperr.h"
#include "../bpipe/core.h"
#include "../bpipe/signal_generator.h"
#include "../bpipe/utils.h"
#include "../lib/Unity/src/unity.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// CHECK_ERR macro for error handling
#define CHECK_ERR(ERR)                                          \
  do {                                                          \
    Bp_EC _ec = ERR;                                            \
    TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, _ec, err_lut[_ec]); \
  } while (false);

/**
 * TestSink_t: A simple sink filter for capturing and validating generated
 * signals
 *
 * This test helper filter consumes batches from the signal generator and stores
 * the samples in a contiguous buffer for analysis. It captures timing
 * information from the first batch and properly handles the completion signal.
 *
 * Fields:
 *   - captured_data: Buffer to store all received samples
 *   - captured_samples: Number of samples received so far
 *   - max_samples: Maximum samples to capture (buffer size)
 *   - first_t_ns: Timestamp of first batch (for timing validation)
 *   - last_t_ns: Timestamp of last sample
 *   - period_ns: Sample period from batch metadata
 */
typedef struct {
  Filter_t base;
  float* captured_data;
  size_t captured_samples;
  size_t max_samples;
  uint64_t first_t_ns;
  uint64_t last_t_ns;
  uint64_t period_ns;
} TestSink_t;

void* test_sink_worker(void* arg)
{
  TestSink_t* sink = (TestSink_t*) arg;
  Bp_EC err;

  long long ts_next = 0;
  while (atomic_load(&sink->base.running)) {
    Batch_t* batch =
        bb_get_tail(&sink->base.input_buffers[0], sink->base.timeout_us, &err);
    if (!batch) {
      if (err == Bp_EC_TIMEOUT) continue;
      if (err == Bp_EC_STOPPED) break;
      break;
    }
    if (ts_next > 0) {
      BP_WORKER_ASSERT(&sink->base, batch->t_ns == ts_next, err);
      ts_next = batch->t_ns + 1 + batch->head - batch->tail;
    }

    if (batch->ec == Bp_EC_COMPLETE) {
      bb_del_tail(&sink->base.input_buffers[0]);
      break;
    }

    // Capture timing info from first batch
    if (sink->captured_samples == 0) {
      sink->first_t_ns = batch->t_ns;
      sink->period_ns = batch->period_ns;
    }

    // Copy samples
    float* data = (float*) batch->data;
    size_t n = batch->head - batch->tail;
    if (sink->captured_samples + n <= sink->max_samples) {
      memcpy(&sink->captured_data[sink->captured_samples], &data[batch->tail],
             n * sizeof(float));
      sink->captured_samples += n;
      sink->last_t_ns = batch->t_ns + (n - 1) * batch->period_ns;
    }

    bb_del_tail(&sink->base.input_buffers[0]);
  }

  return NULL;
}

static Bp_EC test_sink_init(TestSink_t* sink, const char* name,
                            size_t max_samples)
{
  Core_filt_config_t config = {.name = name,
                               .filt_type = FILT_T_MAP,
                               .size = sizeof(TestSink_t),
                               .n_inputs = 1,
                               .max_supported_sinks = 0,
                               .buff_config =
                                   {
                                       .dtype = DTYPE_FLOAT,
                                       .batch_capacity_expo = 6,  // 64 samples
                                       .ring_capacity_expo = 4    // 16 batches
                                   },
                               .timeout_us = 100000,  // 100ms
                               .worker = test_sink_worker};

  Bp_EC err = filt_init(&sink->base, config);
  if (err != Bp_EC_OK) return err;

  sink->captured_data = (float*) calloc(max_samples, sizeof(float));
  if (!sink->captured_data) return Bp_EC_ALLOC;

  sink->max_samples = max_samples;
  sink->captured_samples = 0;

  return Bp_EC_OK;
}

static void test_sink_deinit(TestSink_t* sink)
{
  if (sink->captured_data) {
    free(sink->captured_data);
    sink->captured_data = NULL;
  }
  CHECK_ERR(filt_deinit(&sink->base));
}

/**
 * calculate_rms: Calculate the Root Mean Square value of a signal
 *
 * Used to verify sine wave amplitude. For a sine wave with amplitude A,
 * the RMS value should be A/√2 ≈ 0.707*A
 */
double calculate_rms(float* data, size_t n)
{
  double sum = 0.0;
  for (size_t i = 0; i < n; i++) {
    sum += data[i] * data[i];
  }
  return sqrt(sum / n);
}

/**
 * estimate_frequency: Estimate signal frequency using zero-crossing detection
 *
 * Counts the number of times the signal crosses zero (changes sign) and
 * calculates frequency based on the number of complete periods observed.
 * This method works well for simple periodic signals like sine waves.
 *
 * @param data: Sample buffer
 * @param n: Number of samples
 * @param period_ns: Sample period in nanoseconds
 * @return: Estimated frequency in Hz
 */
double estimate_frequency(float* data, size_t n, uint64_t period_ns)
{
  int zero_crossings = 0;
  for (size_t i = 1; i < n; i++) {
    if ((data[i - 1] < 0 && data[i] >= 0) ||
        (data[i - 1] >= 0 && data[i] < 0)) {
      zero_crossings++;
    }
  }

  // Each period has 2 zero crossings
  double periods = zero_crossings / 2.0;
  double duration_s = n * period_ns * 1e-9;
  return periods / duration_s;
}

void setUp(void) {}
void tearDown(void) {}

/**
 * Test: Signal Generator Initialization
 * Intent: Verify that the signal generator can be properly initialized with
 * valid configuration parameters and that the filter structure is set up
 * correctly. Validates:
 *   - Successful initialization with typical parameters
 *   - Filter type is correctly set to FILT_T_MAP
 *   - Number of input buffers is 0 (source filter)
 *   - Proper cleanup with filt_deinit
 */
void test_signal_generator_init(void)
{
  SignalGenerator_t sg;
  SignalGenerator_config_t config = {
      .name = "test_gen",
      .waveform_type = WAVEFORM_SINE,
      .frequency_hz = 100.0,  // 100 Hz, below Nyquist
      .phase_rad = 0.0,
      .sample_period_ns = 1000000,  // 1 kHz sample rate
      .amplitude = 1.0,
      .offset = 0.0,
      .max_samples = 0,
      .allow_aliasing = false,
      .start_time_ns = 0,
      .timeout_us = 100000,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 4}};

  Bp_EC err = signal_generator_init(&sg, config);
  CHECK_ERR(err);
  TEST_ASSERT_EQUAL(0, sg.base.n_input_buffers);
  TEST_ASSERT_EQUAL(FILT_T_MAP,
                    sg.base.filt_type);  // We're using FILT_T_MAP for now

  CHECK_ERR(filt_deinit(&sg.base));
}

/**
 * Test: Sine Wave Generation
 * Intent: Verify that the signal generator produces a correct sine wave with
 * the specified frequency, amplitude, and sample rate. Validates:
 *   - Correct number of samples generated (4800 samples at 48kHz for 0.1s)
 *   - RMS value is ~0.707 for a unit amplitude sine wave
 *   - Frequency estimation matches configured frequency (1kHz ± 10Hz)
 *   - Proper connection and data flow from generator to sink
 *   - Worker threads complete without errors
 */
void test_sine_wave_generation(void)
{
  SignalGenerator_t sg;
  TestSink_t sink;

  // Initialize generator for 1 kHz sine at 48 kHz sample rate
  SignalGenerator_config_t config = {
      .name = "sine_gen",
      .waveform_type = WAVEFORM_SINE,
      .frequency_hz = 1000.0,
      .phase_rad = 0.0,
      .sample_period_ns = 20833,  // ~48 kHz, Nyquist = 24 kHz
      .amplitude = 1.0,
      .offset = 0.0,
      .max_samples = 4800,  // 0.1 second
      .allow_aliasing = false,
      .start_time_ns = 0,
      .timeout_us = 100000,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 4}};

  CHECK_ERR(signal_generator_init(&sg, config));
  CHECK_ERR(test_sink_init(&sink, "test_sink", 4800));

  // Connect generator to sink
  CHECK_ERR(filt_sink_connect(&sg.base, 0, &sink.base.input_buffers[0]));

  // Start processing
  CHECK_ERR(filt_start(&sg.base));
  CHECK_ERR(filt_start(&sink.base));

  // Wait for completion
  pthread_join(sg.base.worker_thread, NULL);
  pthread_join(sink.base.worker_thread, NULL);

  // Check for worker errors
  CHECK_ERR(sg.base.worker_err_info.ec);
  CHECK_ERR(sink.base.worker_err_info.ec);

  // Verify samples were generated
  TEST_ASSERT_EQUAL(4800, sink.captured_samples);

  // Verify RMS value (should be ~0.707 for sine wave)
  double rms = calculate_rms(sink.captured_data, sink.captured_samples);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 0.707, rms);

  // Verify frequency
  double freq =
      estimate_frequency(sink.captured_data, sink.captured_samples, 20833);
  TEST_ASSERT_FLOAT_WITHIN(10.0, 1000.0, freq);

  // Cleanup
  test_sink_deinit(&sink);
  CHECK_ERR(filt_deinit(&sg.base));
}

/**
 * Test: Square Wave Generation
 * Intent: Verify that the signal generator produces a correct square wave with
 *         proper amplitude scaling and DC offset.
 * Validates:
 *   - Square wave alternates between two discrete values
 *   - Values are correctly scaled by amplitude (2.0) and offset (0.5)
 *   - Expected values are +2.5 (high) and -1.5 (low)
 *   - Roughly equal distribution of high and low samples
 *   - Worker threads complete without errors
 */
void test_square_wave_generation(void)
{
  SignalGenerator_t sg;
  TestSink_t sink;

  SignalGenerator_config_t config = {
      .name = "square_gen",
      .waveform_type = WAVEFORM_SQUARE,
      .frequency_hz = 100.0,
      .phase_rad = 0.0,
      .sample_period_ns = 100000,  // 10 kHz sample rate
      .amplitude = 2.0,
      .offset = 0.5,
      .max_samples = 1000,
      .allow_aliasing = false,
      .start_time_ns = 0,
      .timeout_us = 100000,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 4}};

  CHECK_ERR(signal_generator_init(&sg, config));
  CHECK_ERR(test_sink_init(&sink, "test_sink", 1000));
  CHECK_ERR(filt_sink_connect(&sg.base, 0, &sink.base.input_buffers[0]));

  CHECK_ERR(filt_start(&sg.base));
  CHECK_ERR(filt_start(&sink.base));

  pthread_join(sg.base.worker_thread, NULL);
  pthread_join(sink.base.worker_thread, NULL);

  // Check for worker errors
  CHECK_ERR(sg.base.worker_err_info.ec);
  CHECK_ERR(sink.base.worker_err_info.ec);

  // Verify square wave values (should be +2.5 or -1.5 with amplitude=2,
  // offset=0.5)
  int high_count = 0, low_count = 0;
  for (size_t i = 0; i < sink.captured_samples; i++) {
    if (fabs(sink.captured_data[i] - 2.5) < 0.01)
      high_count++;
    else if (fabs(sink.captured_data[i] - (-1.5)) < 0.01)
      low_count++;
  }

  // Should have roughly equal high and low samples
  TEST_ASSERT_GREATER_THAN(400, high_count);
  TEST_ASSERT_GREATER_THAN(400, low_count);
  TEST_ASSERT_EQUAL(1000, high_count + low_count);

  test_sink_deinit(&sink);
  CHECK_ERR(filt_deinit(&sg.base));
}

/**
 * Test: Phase Continuity Across Batches
 * Intent: Verify that waveform phase remains continuous when samples span
 * multiple batches, ensuring no phase jumps or discontinuities at batch
 * boundaries. Uses a sawtooth wave for predictable linear values.
 *
 * Validates:
 *   - Initial phase offset (π/2 radians = 25% of period) is correctly applied
 *   - Sawtooth values progress linearly from -1 to +1 over each period
 *   - No phase reset or jump between batch 63→64 (with 64-sample batches)
 *   - Time-based generation maintains exact phase continuity
 *
 * Test parameters:
 *   - 100 Hz sawtooth at 10 kHz sample rate = 100 samples per period
 *   - Each sample advances by 1/100 of a period = 2/100 = 0.02 in amplitude
 *   - Starting at phase π/2 (25% into period), first value should be -0.5
 */
void test_phase_continuity(void)
{
  SignalGenerator_t sg;
  TestSink_t sink;

  // Generate 2 batches worth of samples with sawtooth
  SignalGenerator_config_t config = {
      .name = "phase_test",
      .waveform_type = WAVEFORM_SAWTOOTH,
      .frequency_hz = 100.0,       // 100 Hz = 100 samples per period at 10kHz
      .phase_rad = M_PI / 2,       // Start 25% into the period
      .sample_period_ns = 100000,  // 10 kHz sample rate
      .amplitude = 1.0,
      .offset = 0.0,
      .max_samples = 128,  // 2 batches of 64
      .allow_aliasing = false,
      .start_time_ns = 0,
      .timeout_us = 100000,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,  // 64 samples
                      .ring_capacity_expo = 4}};

  CHECK_ERR(signal_generator_init(&sg, config));
  CHECK_ERR(test_sink_init(&sink, "test_sink", 128));
  CHECK_ERR(filt_sink_connect(&sg.base, 0, &sink.base.input_buffers[0]));

  CHECK_ERR(filt_start(&sg.base));
  CHECK_ERR(filt_start(&sink.base));

  pthread_join(sg.base.worker_thread, NULL);
  pthread_join(sink.base.worker_thread, NULL);

  // Check for worker errors
  CHECK_ERR(sg.base.worker_err_info.ec);
  CHECK_ERR(sink.base.worker_err_info.ec);

  // Verify we got the expected number of samples
  TEST_ASSERT_EQUAL(128, sink.captured_samples);

  // Sawtooth goes from -1 to +1 over one period (100 samples)
  // Each sample increases by 2/100 = 0.02
  float expected_increment = 2.0f / 100.0f;

  // Starting at phase π/2 (25% of period), initial value should be -0.5
  // (sawtooth is at -1 + 0.25 * 2 = -0.5)
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.5f, sink.captured_data[0]);

  // Verify linear progression for first few samples
  for (int i = 1; i < 10; i++) {
    float expected = sink.captured_data[0] + i * expected_increment;
    // Handle wrap-around from +1 to -1
    if (expected > 1.0f) expected -= 2.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, expected, sink.captured_data[i]);
  }

  // Critical test: Verify continuity at batch boundary (samples 63 and 64)
  // The difference should be exactly the expected increment
  float boundary_diff = sink.captured_data[64] - sink.captured_data[63];

  // Handle the case where we wrap from near +1 to near -1
  if (boundary_diff < -1.5f) {
    boundary_diff += 2.0f;  // Correct for wrap-around
  }

  TEST_ASSERT_FLOAT_WITHIN(0.0001f, expected_increment, boundary_diff);

  // Also verify a few samples after the boundary
  for (int i = 65; i < 70 && i < sink.captured_samples; i++) {
    float expected_diff = sink.captured_data[i] - sink.captured_data[i - 1];
    if (expected_diff < -1.5f) expected_diff += 2.0f;  // Handle wrap
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, expected_increment, expected_diff);
  }

  test_sink_deinit(&sink);
  CHECK_ERR(filt_deinit(&sg.base));
}

/**
 * Test: Nyquist Frequency Validation
 * Intent: Verify that the signal generator properly validates the Nyquist
 * criterion and fails gracefully when the requested frequency exceeds the
 * Nyquist limit. Validates:
 *   - Generator detects when frequency > sample_rate/2
 *   - Worker thread reports Bp_EC_INVALID_CONFIG error
 *   - Filter starts successfully but worker fails during validation
 *   - Error is properly propagated through worker_err_info
 * Test case: 6kHz signal at 10kHz sample rate (Nyquist = 5kHz)
 */
void test_nyquist_validation(void)
{
  SignalGenerator_t sg;

  // Try to generate 6 kHz at 10 kHz sample rate (exceeds Nyquist)
  SignalGenerator_config_t config = {
      .name = "nyquist_test",
      .waveform_type = WAVEFORM_SINE,
      .frequency_hz = 6000.0,
      .phase_rad = 0.0,
      .sample_period_ns = 100000,  // 10 kHz sample rate
      .amplitude = 1.0,
      .offset = 0.0,
      .max_samples = 100,
      .allow_aliasing = false,  // Should fail
      .start_time_ns = 0,
      .timeout_us = 100000,
      .buff_config = {.dtype = DTYPE_FLOAT,
                      .batch_capacity_expo = 6,
                      .ring_capacity_expo = 4}};

  CHECK_ERR(signal_generator_init(&sg, config));

  // Create sink but don't expect data
  TestSink_t sink;
  CHECK_ERR(test_sink_init(&sink, "test_sink", 100));
  CHECK_ERR(filt_sink_connect(&sg.base, 0, &sink.base.input_buffers[0]));

  // Start should succeed but worker should detect error
  CHECK_ERR(filt_start(&sg.base));
  CHECK_ERR(filt_start(&sink.base));

  CHECK_ERR(filt_stop(&sg.base));
  CHECK_ERR(filt_stop(&sink.base));

  pthread_join(sg.base.worker_thread, NULL);
  pthread_join(sink.base.worker_thread, NULL);

  // Should have error in worker (Nyquist validation should fail)
  TEST_ASSERT_EQUAL(Bp_EC_INVALID_CONFIG, sg.base.worker_err_info.ec);
  // Sink might or might not have error depending on timing
  // Don't check sink worker error as it depends on when the signal generator
  // stops

  test_sink_deinit(&sink);
  CHECK_ERR(filt_deinit(&sg.base));
}

/**
 * Test: All Waveform Types
 * Intent: Verify that all supported waveform types (SINE, SQUARE, SAWTOOTH,
 * TRIANGLE) generate valid output within expected amplitude ranges. Validates:
 *   - Each waveform type can be initialized and generates samples
 *   - All samples fall within [-1.1, 1.1] range (allowing small floating point
 * tolerance)
 *   - No crashes or errors when switching between waveform types
 *   - Worker threads complete successfully for all waveform types
 * Note: This is a basic smoke test; detailed waveform validation is done in
 * specific tests
 */
void test_all_waveforms(void)
{
  WaveformType_e waveforms[] = {WAVEFORM_SINE, WAVEFORM_SQUARE,
                                WAVEFORM_SAWTOOTH, WAVEFORM_TRIANGLE};

  for (int w = 0; w < 4; w++) {
    SignalGenerator_t sg;
    TestSink_t sink;

    SignalGenerator_config_t config = {
        .name = "waveform_test",
        .waveform_type = waveforms[w],
        .frequency_hz = 250.0,
        .phase_rad = 0.0,
        .sample_period_ns = 100000,  // 10 kHz
        .amplitude = 1.0,
        .offset = 0.0,
        .max_samples = 400,  // 40ms
        .allow_aliasing = false,
        .start_time_ns = 0,
        .timeout_us = 100000,
        .buff_config = {.dtype = DTYPE_FLOAT,
                        .batch_capacity_expo = 6,
                        .ring_capacity_expo = 4}};

    CHECK_ERR(signal_generator_init(&sg, config));
    CHECK_ERR(test_sink_init(&sink, "test_sink", 400));
    CHECK_ERR(filt_sink_connect(&sg.base, 0, &sink.base.input_buffers[0]));

    CHECK_ERR(filt_start(&sg.base));
    CHECK_ERR(filt_start(&sink.base));

    pthread_join(sg.base.worker_thread, NULL);
    pthread_join(sink.base.worker_thread, NULL);

    // Check for worker errors
    CHECK_ERR(sg.base.worker_err_info.ec);
    CHECK_ERR(sink.base.worker_err_info.ec);

    // Basic validation - all samples should be within [-1, 1]
    for (size_t i = 0; i < sink.captured_samples; i++) {
      TEST_ASSERT_TRUE(sink.captured_data[i] >= -1.1);
      TEST_ASSERT_TRUE(sink.captured_data[i] <= 1.1);
    }

    test_sink_deinit(&sink);
    CHECK_ERR(filt_deinit(&sg.base));
  }
}

int main(void)
{
  UNITY_BEGIN();

  RUN_TEST(test_signal_generator_init);
  RUN_TEST(test_sine_wave_generation);
  RUN_TEST(test_square_wave_generation);
  RUN_TEST(test_phase_continuity);
  RUN_TEST(test_nyquist_validation);
  RUN_TEST(test_all_waveforms);

  return UNITY_END();
}
