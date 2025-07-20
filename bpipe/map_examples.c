#include <string.h>
#include "map.h"

/* =============================================================================
 * Example Map Functions
 * =============================================================================
 */

Bp_EC map_identity_f32(const void* in, void* out, size_t n_samples)
{
  if (!in || !out) return Bp_EC_NULL_POINTER;

  const float* input = (const float*) in;
  float* output = (float*) out;

  for (size_t i = 0; i < n_samples; i++) {
    output[i] = input[i];
  }

  return Bp_EC_OK;
}

Bp_EC map_identity_memcpy(const void* in, void* out, size_t n_samples)
{
  if (!in || !out) return Bp_EC_NULL_POINTER;

  // Note: This assumes float data type
  // In practice, would need data width parameter
  memcpy(out, in, n_samples * sizeof(float));

  return Bp_EC_OK;
}