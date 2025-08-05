#include "bperr.h"

char err_lut[Bp_EC_MAX][100] = {
    ERR_LUT_ENTRY(OK),
    ERR_LUT_ENTRY(COMPLETE),
    ERR_LUT_ENTRY(STOPPED),
    ERR_LUT_ENTRY(TIMEOUT),
    ERR_LUT_ENTRY(PTHREAD_UNKOWN),
    ERR_LUT_ENTRY(NOINPUT),
    ERR_LUT_ENTRY(NOSPACE),
    ERR_LUT_ENTRY(GET_HEAD_NULL),
    ERR_LUT_ENTRY(TYPE_MISMATCH),
    ERR_LUT_ENTRY(BAD_PYOBJECT),
    ERR_LUT_ENTRY(COND_INIT_FAIL),
    ERR_LUT_ENTRY(MUTEX_INIT_FAIL),
    ERR_LUT_ENTRY(NULL_FILTER),
    ERR_LUT_ENTRY(ALREADY_RUNNING),
    ERR_LUT_ENTRY(THREAD_CREATE_FAIL),
    ERR_LUT_ENTRY(CAPACITY_MISMATCH),
    ERR_LUT_ENTRY(THREAD_JOIN_FAIL),
    ERR_LUT_ENTRY(DTYPE_MISMATCH), /* Source/sink data types don't match */
    ERR_LUT_ENTRY(WIDTH_MISMATCH), /* Data width mismatch */
    ERR_LUT_ENTRY(DTYPE_INVALID),
    ERR_LUT_ENTRY(INVALID_DTYPE),         /* Invalid or unsupported data type */
    ERR_LUT_ENTRY(INVALID_CONFIG),        /* Invalid configuration parameters */
    ERR_LUT_ENTRY(INVALID_CONFIG_WORKER), /* Invalid configuration parameters */
    ERR_LUT_ENTRY(
        INVALID_CONFIG_MAX_INPUTS), /* Invalid configuration parameters */
    ERR_LUT_ENTRY(
        INVALID_CONFIG_MAX_SINKS), /* Invalid configuration parameters */
    ERR_LUT_ENTRY(
        INVALID_CONFIG_FILTER_SIZE), /* Invalid configuration parameters */
    ERR_LUT_ENTRY(
        INVALID_CONFIG_FILTER_T), /* Invalid configuration parameters */
    ERR_LUT_ENTRY(
        INVALID_CONFIG_TIMEOUT),    /* Invalid configuration parameters */
    ERR_LUT_ENTRY(CONFIG_REQUIRED), /* Configuration missing required fields */
    ERR_LUT_ENTRY(MALLOC_FAIL),     /* Memory allocation failure */
    ERR_LUT_ENTRY(MEMCPY_FAIL),     /* Memory copy failure */
    ERR_LUT_ENTRY(MEMSET_FAIL),     /* Memory set failure */
    ERR_LUT_ENTRY(THREAD_CREATE_NAME_FAIL), /* Failed to name thread */
    ERR_LUT_ENTRY(BUFFER_EMPTY),
    ERR_LUT_ENTRY(CONNECTION_OCCUPIED),
    ERR_LUT_ENTRY(INVALID_SINK_IDX),
    ERR_LUT_ENTRY(NULL_BUFF),
    ERR_LUT_ENTRY(ALREADY_REGISTERED),
    ERR_LUT_ENTRY(NOT_IMPLEMENTED),
    ERR_LUT_ENTRY(NULL_POINTER),
    ERR_LUT_ENTRY(NO_SINK),
    ERR_LUT_ENTRY(PHASE_ERROR),
    ERR_LUT_ENTRY(TYPE_ERROR),
    ERR_LUT_ENTRY(ALLOC),
    ERR_LUT_ENTRY(INVALID_DATA),
    ERR_LUT_ENTRY(NO_SPACE),
    ERR_LUT_ENTRY(INVALID_PRECISION),
    ERR_LUT_ENTRY(FILTER_STOPPING),
    ERR_LUT_ENTRY(PROPERTY_MISMATCH),
};
