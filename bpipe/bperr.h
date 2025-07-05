#ifndef BPERR_H
#define BPERR_H

typedef enum _Bp_EC {
    /* Success */
    Bp_EC_OK = 0,
    /* Positive status codes */
    Bp_EC_COMPLETE = 1, /* Stream termination sentinel */
    Bp_EC_STOPPED = 2,  /* Buffer has been stopped */
    /* Negative error codes */
    Bp_EC_TIMEOUT = -1,
    Bp_EC_NOINPUT = -2,
    Bp_EC_NOSPACE = -3,
    Bp_EC_TYPE_MISMATCH = -4,
    Bp_EC_BAD_PYOBJECT = -5,
    Bp_EC_COND_INIT_FAIL = -6,
    Bp_EC_MUTEX_INIT_FAIL = -7,
    Bp_EC_NULL_FILTER = -8,
    Bp_EC_ALREADY_RUNNING = -9,
    Bp_EC_THREAD_CREATE_FAIL = -10,
    Bp_EC_THREAD_JOIN_FAIL = -11,
    Bp_EC_DTYPE_MISMATCH = -12,  /* Source/sink data types don't match */
    Bp_EC_WIDTH_MISMATCH = -13,  /* Data width mismatch */
    Bp_EC_INVALID_DTYPE = -14,   /* Invalid or unsupported data type */
    Bp_EC_INVALID_CONFIG = -15,  /* Invalid configuration parameters */
    Bp_EC_CONFIG_REQUIRED = -16, /* Configuration missing required fields */
    Bp_EC_MALLOC_FAIL = -17,     /* Memory allocation failure */
    Bp_EC_THREAD_CREATE_NAME_FAIL = -18, /* Failed to name thread */
    Bp_EC_BUFFER_EMPTY = -19,
} Bp_EC;


typedef struct _err_info {
    Bp_EC ec;
    int line_no;
    const char* filename;
    const char* function;
    const char* err_msg;
} Err_info;

#endif /* BPIPE_CORE_H */
