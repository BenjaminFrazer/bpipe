#ifndef BPIPE_UTILS_H
#define BPIPE_UTILS_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

/* Common math macros */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* Batch processing utilities */
#define NEEDS_NEW_BATCH(batch) (!batch || batch->tail >= batch->head)
#define BATCH_FULL(batch, size) (batch->head >= size)

/* Worker thread assertion macro for error handling
 * Sets error info and exits worker thread on assertion failure
 * Usage: BP_WORKER_ASSERT(filter_ptr, condition, error_code)
 */
#define BP_WORKER_ASSERT(f, cond, err)              \
  do {                                              \
    if (!(cond)) {                                  \
      (f)->worker_err_info.line_no = __LINE__;      \
      (f)->worker_err_info.function = __FUNCTION__; \
      (f)->worker_err_info.filename = __FILE__;     \
      (f)->worker_err_info.ec = err;                \
      atomic_store(&(f)->running, false);           \
      return NULL;                                  \
    }                                               \
  } while (false)

#endif /* BPIPE_UTILS_H */

#define PI 3.1415  // TODO
