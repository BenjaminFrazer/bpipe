#ifndef SAMPLE_ALIGNER_H
#define SAMPLE_ALIGNER_H

#include "core.h"
#include "batch_buffer.h"
#include "bperr.h"

// Interpolation methods
typedef enum {
    INTERP_NEAREST,  // Round to nearest grid point (lowest latency)
    INTERP_LINEAR,   // Linear interpolation between samples
    INTERP_CUBIC,    // Cubic spline for smoother signals
    INTERP_SINC      // Ideal reconstruction (highest quality, most latency)
} InterpolationMethod_e;

// Alignment strategies for first sample
typedef enum {
    ALIGN_NEAREST,   // Minimize time shift (default)
    ALIGN_BACKWARD,  // Preserve all data (may extrapolate)
    ALIGN_FORWARD    // Never extrapolate (may skip initial data)
} AlignmentStrategy_e;

// Boundary handling for interpolation
typedef enum {
    BOUNDARY_HOLD,    // Repeat edge values
    BOUNDARY_REFLECT, // Mirror data at boundaries
    BOUNDARY_ZERO     // Assume zero outside bounds
} BoundaryHandling_e;

// Configuration struct
typedef struct {
    const char* name;
    BatchBuffer_config buff_config;
    long timeout_us;  // Optional timeout
    
    // Interpolation settings
    InterpolationMethod_e method;      // NEAREST, LINEAR, CUBIC, SINC
    AlignmentStrategy_e alignment;     // NEAREST, BACKWARD, FORWARD
    BoundaryHandling_e boundary;       // HOLD, REFLECT, ZERO
    
    // For SINC method
    size_t sinc_taps;                 // Filter length (0 = auto)
    float sinc_cutoff;                // Normalized cutoff (0-1)
} SampleAligner_config_t;

// Filter struct
typedef struct {
    Filter_t base;  // MUST be first member
    
    // Configuration
    InterpolationMethod_e method;
    AlignmentStrategy_e alignment;
    BoundaryHandling_e boundary;
    size_t sinc_taps;
    float sinc_cutoff;
    
    // Runtime state
    uint64_t period_ns;              // From first input
    uint64_t next_output_ns;         // Next aligned timestamp
    bool initialized;                // First batch processed
    
    // Interpolation buffer
    void* history_buffer;            // Previous samples for interpolation
    size_t history_size;             // Based on method
    size_t history_samples;          // Current samples in history
    
    // Statistics
    uint64_t samples_interpolated;
    uint64_t max_phase_correction_ns;
    uint64_t total_phase_correction_ns;
} SampleAligner_t;

// Initialize the filter
Bp_EC sample_aligner_init(SampleAligner_t* f, SampleAligner_config_t config);

#endif // SAMPLE_ALIGNER_H