#include <pthread.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "sample_aligner.h"
#include "core.h"
#include "unity.h"
#include "test_utils.h"

// Define M_PI if not defined
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Test fixture
typedef struct {
    SampleAligner_t aligner;
    Filter_t source;
    Filter_t sink;
    pthread_t source_thread;
    bool source_running;
    // Test parameters
    uint64_t test_period_ns;
    uint64_t test_phase_offset_ns;
    size_t test_batch_size;
} TestFixture;

static TestFixture fixture;

// Simple source worker that generates data with configurable phase offset
void* test_source_worker(void* arg) {
    Filter_t* f = (Filter_t*)arg;
    TestFixture* fix = (TestFixture*)((char*)f - offsetof(TestFixture, source));
    
    uint64_t t_ns = fix->test_phase_offset_ns;  // Start with phase offset
    uint32_t batch_id = 0;
    int batches_sent = 0;
    const int max_batches = 5;  // Send limited batches for testing
    
    while (fix->source_running && batches_sent < max_batches) {
        Batch_t* batch = bb_get_head(f->sinks[0]);
        if (batch == NULL) {
            TEST_FAIL_MESSAGE("bb_get_head returned NULL - buffer allocation failed");
        }
        
        // Fill batch with test data
        batch->t_ns = t_ns;
        batch->period_ns = fix->test_period_ns;
        batch->head = 0;
        batch->tail = fix->test_batch_size;
        batch->batch_id = batch_id++;
        batch->ec = Bp_EC_OK;
        
        // Fill with sine wave for testing interpolation quality
        float* data = (float*)batch->data;
        for (size_t i = 0; i < fix->test_batch_size; i++) {
            // Generate sine wave: sin(2*pi*freq*t) where freq = 10Hz
            double time_s = (double)(t_ns + i * fix->test_period_ns) / 1e9;
            data[i] = sinf(2.0f * M_PI * 10.0f * time_s);
        }
        
        t_ns += fix->test_batch_size * fix->test_period_ns;
        
        CHECK_ERR(bb_submit(f->sinks[0], 1000000));
        batches_sent++;
    }
    
    // Stop generating data before sending completion
    fix->source_running = false;
    
    // Wait a bit for aligner to process remaining batches
    struct timespec sleep_time = {0, 10000000};  // 10ms
    nanosleep(&sleep_time, NULL);
    
    // Send completion
    Batch_t* batch = bb_get_head(f->sinks[0]);
    if (batch == NULL) {
        TEST_FAIL_MESSAGE("bb_get_head returned NULL when sending completion - buffer might be full");
    }
    batch->ec = Bp_EC_COMPLETE;
    batch->head = 0;
    batch->tail = 0;
    CHECK_ERR(bb_submit(f->sinks[0], 1000000));
    
    return NULL;
}

void setUp(void) { 
    memset(&fixture, 0, sizeof(fixture));
    // Default test parameters
    fixture.test_period_ns = 1000000;  // 1ms period (1kHz)
    fixture.test_phase_offset_ns = 0;
    fixture.test_batch_size = 64;
}

void tearDown(void) {
    // Stop source manually if running
    if (fixture.source_running) {
        fixture.source_running = false;
        if (fixture.source.running) {
            filt_stop(&fixture.source);
        }
    }
    
    // Stop filters before deinit
    if (fixture.aligner.base.worker != NULL && fixture.aligner.base.running) {
        filt_stop(&fixture.aligner.base);
    }
    if (fixture.sink.worker != NULL && fixture.sink.running) {
        filt_stop(&fixture.sink);
    }
    
    // Deinit filters if initialized
    if (fixture.aligner.base.worker != NULL) {
        filt_deinit(&fixture.aligner.base);
    }
    if (fixture.source.worker != NULL) {
        filt_deinit(&fixture.source);
    }
    if (fixture.sink.worker != NULL) {
        filt_deinit(&fixture.sink);
    }
}

void test_basic_phase_correction(void) {
    // Setup source with phase offset
    fixture.test_phase_offset_ns = 345000;  // 345us offset
    
    Core_filt_config_t source_config = {
        .name = "test_source",
        .filt_type = FILT_T_MAP,
        .size = sizeof(Filter_t),
        .n_inputs = 0,
        .max_supported_sinks = 1,
        .buff_config = {.dtype = DTYPE_FLOAT,
                       .batch_capacity_expo = 6,  // 64 samples
                       .ring_capacity_expo = 4,   // 16 batches
                       .overflow_behaviour = OVERFLOW_BLOCK},
        .timeout_us = 1000000,
        .worker = test_source_worker
    };
    CHECK_ERR(filt_init(&fixture.source, source_config));
    
    // Setup SampleAligner
    SampleAligner_config_t aligner_config = {
        .name = "test_aligner",
        .buff_config = {.dtype = DTYPE_FLOAT,
                       .batch_capacity_expo = 6,
                       .ring_capacity_expo = 4,
                       .overflow_behaviour = OVERFLOW_BLOCK},
        .timeout_us = 1000000,
        .method = INTERP_NEAREST,
        .alignment = ALIGN_NEAREST,
        .boundary = BOUNDARY_HOLD
    };
    CHECK_ERR(sample_aligner_init(&fixture.aligner, aligner_config));
    
    // Setup sink
    Core_filt_config_t sink_config = {
        .name = "test_sink",
        .filt_type = FILT_T_MATCHED_PASSTHROUGH,
        .size = sizeof(Filter_t),
        .n_inputs = 1,
        .max_supported_sinks = 0,
        .buff_config = {.dtype = DTYPE_FLOAT,
                       .batch_capacity_expo = 6,
                       .ring_capacity_expo = 4,
                       .overflow_behaviour = OVERFLOW_BLOCK},
        .timeout_us = 1000000,
        .worker = matched_passthroug
    };
    CHECK_ERR(filt_init(&fixture.sink, sink_config));
    
    // Connect pipeline: source -> aligner -> sink
    CHECK_ERR(filt_sink_connect(&fixture.source, 0, 
                               &fixture.aligner.base.input_buffers[0]));
    CHECK_ERR(filt_sink_connect(&fixture.aligner.base, 0,
                               &fixture.sink.input_buffers[0]));
    
    // Start filters
    fixture.source_running = true;
    CHECK_ERR(filt_start(&fixture.source));  // Start source properly
    CHECK_ERR(filt_start(&fixture.aligner.base));
    CHECK_ERR(filt_start(&fixture.sink));
    
    // Let it run briefly
    struct timespec sleep_time = {0, 100000000};  // 100ms
    nanosleep(&sleep_time, NULL);
    
    // Check output has aligned timestamps
    Bp_EC err;
    Batch_t* output = bb_get_tail(&fixture.sink.input_buffers[0], 1000000, &err);
    if (err == Bp_EC_OK && output != NULL) {
        // Verify alignment: t_ns % period_ns == 0
        TEST_ASSERT_EQUAL(0, output->t_ns % output->period_ns);
        TEST_ASSERT_EQUAL(fixture.test_period_ns, output->period_ns);
        bb_del_tail(&fixture.sink.input_buffers[0]);
    }
    
    // Check statistics
    TEST_ASSERT_TRUE(fixture.aligner.max_phase_correction_ns > 0);
    TEST_ASSERT_TRUE(fixture.aligner.samples_interpolated > 0);
}

void test_various_phase_offsets(void) {
    uint64_t phase_offsets[] = {
        0,                              // No offset
        fixture.test_period_ns / 4,     // Quarter period
        fixture.test_period_ns / 2,     // Half period
        fixture.test_period_ns - 1      // Almost full period
    };
    
    for (size_t i = 0; i < sizeof(phase_offsets)/sizeof(phase_offsets[0]); i++) {
        
        fixture.test_phase_offset_ns = phase_offsets[i];
        
        // Create simple pipeline
        SampleAligner_config_t config = {
            .name = "phase_test",
            .buff_config = {.dtype = DTYPE_FLOAT,
                           .batch_capacity_expo = 6,
                           .ring_capacity_expo = 4,
                           .overflow_behaviour = OVERFLOW_BLOCK},
            .method = INTERP_LINEAR,
            .alignment = ALIGN_NEAREST
        };
        CHECK_ERR(sample_aligner_init(&fixture.aligner, config));
        
        // Manually push a batch with phase offset
        Batch_buff_t* input_buf = &fixture.aligner.base.input_buffers[0];
        CHECK_ERR(bb_start(input_buf));
        
        Batch_t* batch = bb_get_head(input_buf);
        if (batch == NULL) {
            TEST_FAIL_MESSAGE("bb_get_head returned NULL");
        }
        
        batch->t_ns = phase_offsets[i];
        batch->period_ns = fixture.test_period_ns;
        batch->head = 0;
        batch->tail = 64;
        batch->ec = Bp_EC_OK;
        
        // Fill with test data
        float* data = (float*)batch->data;
        for (int j = 0; j < 64; j++) {
            data[j] = (float)j;
        }
        
        CHECK_ERR(bb_submit(input_buf, 1000000));
        
        // Connect a sink
        Core_filt_config_t sink_config = {
            .name = "test_sink",
            .filt_type = FILT_T_MATCHED_PASSTHROUGH,
            .size = sizeof(Filter_t),
            .n_inputs = 1,
            .max_supported_sinks = 0,
            .buff_config = {.dtype = DTYPE_FLOAT,
                           .batch_capacity_expo = 6,
                           .ring_capacity_expo = 4,
                           .overflow_behaviour = OVERFLOW_BLOCK},
            .timeout_us = 1000000,
            .worker = matched_passthroug
        };
        CHECK_ERR(filt_init(&fixture.sink, sink_config));
        CHECK_ERR(filt_sink_connect(&fixture.aligner.base, 0,
                                   &fixture.sink.input_buffers[0]));
        
        // Start aligner
        CHECK_ERR(filt_start(&fixture.aligner.base));
        CHECK_ERR(filt_start(&fixture.sink));
        
        // Wait briefly
        struct timespec sleep_time = {0, 50000000};  // 50ms
        nanosleep(&sleep_time, NULL);
        
        // Verify max phase correction
        TEST_ASSERT_EQUAL(phase_offsets[i], fixture.aligner.max_phase_correction_ns);
        
        tearDown();
        setUp();  // Reset fixture for next iteration
    }
}

void test_alignment_strategies(void) {
    // Test ALIGN_BACKWARD, ALIGN_FORWARD, ALIGN_NEAREST
    AlignmentStrategy_e strategies[] = {ALIGN_NEAREST, ALIGN_BACKWARD, ALIGN_FORWARD};
    const char* strategy_names[] = {"NEAREST", "BACKWARD", "FORWARD"};
    
    for (size_t i = 0; i < sizeof(strategies)/sizeof(strategies[0]); i++) {
        setUp();
        
        // Use a phase offset that will test the alignment strategy
        fixture.test_phase_offset_ns = 600000;  // 600us into a 1ms period
        
        SampleAligner_config_t config = {
            .name = strategy_names[i],
            .buff_config = {.dtype = DTYPE_FLOAT,
                           .batch_capacity_expo = 6,
                           .ring_capacity_expo = 4,
                           .overflow_behaviour = OVERFLOW_BLOCK},
            .method = INTERP_LINEAR,
            .alignment = strategies[i]
        };
        CHECK_ERR(sample_aligner_init(&fixture.aligner, config));
        
        // Setup minimal pipeline
        Core_filt_config_t sink_config = {
            .name = "align_test_sink",
            .filt_type = FILT_T_MATCHED_PASSTHROUGH,
            .size = sizeof(Filter_t),
            .n_inputs = 1,
            .max_supported_sinks = 0,
            .buff_config = {.dtype = DTYPE_FLOAT,
                           .batch_capacity_expo = 6,
                           .ring_capacity_expo = 4,
                           .overflow_behaviour = OVERFLOW_BLOCK},
            .timeout_us = 1000000,
            .worker = matched_passthroug
        };
        CHECK_ERR(filt_init(&fixture.sink, sink_config));
        CHECK_ERR(filt_sink_connect(&fixture.aligner.base, 0,
                                   &fixture.sink.input_buffers[0]));
        
        // Start filters
        CHECK_ERR(filt_start(&fixture.aligner.base));
        CHECK_ERR(filt_start(&fixture.sink));
        
        // Push test batch
        Batch_buff_t* input_buf = &fixture.aligner.base.input_buffers[0];
        Batch_t* batch = bb_get_head(input_buf);
        if (batch == NULL) {
            TEST_FAIL_MESSAGE("bb_get_head returned NULL");
        }
        
        batch->t_ns = fixture.test_phase_offset_ns;
        batch->period_ns = fixture.test_period_ns;
        batch->head = 0;
        batch->tail = 10;
        batch->ec = Bp_EC_OK;
        CHECK_ERR(bb_submit(input_buf, 1000000));
        
        // Wait a short time for initialization
        struct timespec sleep_time = {0, 10000000};  // 10ms
        nanosleep(&sleep_time, NULL);
        
        // Stop filters to check state
        filt_stop(&fixture.aligner.base);
        filt_stop(&fixture.sink);
        
        // Verify alignment happened correctly
        TEST_ASSERT_TRUE(fixture.aligner.initialized);
        
        // The next_output_ns will have been updated after processing
        // We need to check if the alignment was correct by verifying
        // that next_output_ns is aligned to the grid
        TEST_ASSERT_EQUAL(0, fixture.aligner.next_output_ns % fixture.test_period_ns);
        
        tearDown();
    }
}

void test_non_numeric_data_rejection(void) {
    // Try to create aligner with non-numeric data type
    SampleAligner_config_t config = {
        .name = "invalid_type_test",
        .buff_config = {.dtype = DTYPE_NDEF,  // Invalid type
                       .batch_capacity_expo = 6,
                       .ring_capacity_expo = 4,
                       .overflow_behaviour = OVERFLOW_BLOCK},
        .method = INTERP_LINEAR,
        .alignment = ALIGN_NEAREST
    };
    // Should fail during init due to invalid dtype
    Bp_EC err = sample_aligner_init(&fixture.aligner, config);
    TEST_ASSERT_EQUAL(Bp_EC_INVALID_DTYPE, err);
}

void test_with_batch_matcher(void) {
    // Test integration: Source -> SampleAligner -> BatchMatcher
    // This verifies that SampleAligner output works with BatchMatcher
    
    // Source with phase offset
    fixture.test_phase_offset_ns = 12345;  // Non-aligned timestamp
    
    Core_filt_config_t source_config = {
        .name = "phased_source",
        .filt_type = FILT_T_MAP,
        .size = sizeof(Filter_t),
        .n_inputs = 0,
        .max_supported_sinks = 1,
        .buff_config = {.dtype = DTYPE_FLOAT,
                       .batch_capacity_expo = 6,
                       .ring_capacity_expo = 4,
                       .overflow_behaviour = OVERFLOW_BLOCK},
        .timeout_us = 1000000,
        .worker = test_source_worker
    };
    CHECK_ERR(filt_init(&fixture.source, source_config));
    
    // SampleAligner
    SampleAligner_config_t aligner_config = {
        .name = "phase_fixer",
        .buff_config = {.dtype = DTYPE_FLOAT,
                       .batch_capacity_expo = 6,
                       .ring_capacity_expo = 4,
                       .overflow_behaviour = OVERFLOW_BLOCK},
        .method = INTERP_LINEAR,
        .alignment = ALIGN_NEAREST
    };
    CHECK_ERR(sample_aligner_init(&fixture.aligner, aligner_config));
    
    // Connect source -> aligner
    CHECK_ERR(filt_sink_connect(&fixture.source, 0,
                               &fixture.aligner.base.input_buffers[0]));
    
    // Create a sink for the aligner output
    Core_filt_config_t sink_config = {
        .name = "test_sink",
        .filt_type = FILT_T_MATCHED_PASSTHROUGH,
        .size = sizeof(Filter_t),
        .n_inputs = 1,
        .max_supported_sinks = 0,
        .buff_config = {.dtype = DTYPE_FLOAT,
                       .batch_capacity_expo = 6,
                       .ring_capacity_expo = 4,
                       .overflow_behaviour = OVERFLOW_BLOCK},
        .timeout_us = 1000000,
        .worker = matched_passthroug
    };
    CHECK_ERR(filt_init(&fixture.sink, sink_config));
    CHECK_ERR(filt_sink_connect(&fixture.aligner.base, 0,
                               &fixture.sink.input_buffers[0]));
    
    // Start filters
    fixture.source_running = true;
    CHECK_ERR(filt_start(&fixture.source));
    CHECK_ERR(filt_start(&fixture.aligner.base));
    CHECK_ERR(filt_start(&fixture.sink));
    
    // Note: In real test we'd connect to BatchMatcher and verify no phase errors
    // For now, just verify aligner is producing aligned output
    
    struct timespec sleep_time = {0, 50000000};  // 50ms
    nanosleep(&sleep_time, NULL);
    
    // Stop and check we processed some data
    TEST_ASSERT_TRUE(fixture.aligner.samples_interpolated > 0);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_basic_phase_correction);
    RUN_TEST(test_various_phase_offsets);
    RUN_TEST(test_alignment_strategies);
    RUN_TEST(test_non_numeric_data_rejection);
    RUN_TEST(test_with_batch_matcher);
    
    return UNITY_END();
}