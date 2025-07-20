#ifndef BPERR_H
#define BPERR_H

typedef enum _Bp_EC {
  /* Success */
  Bp_EC_OK = 0,
  /* Positive status codes */
  Bp_EC_COMPLETE = 1, /* Stream termination sentinel */
  Bp_EC_STOPPED = 2,  /* Buffer has been stopped */
  Bp_EC_TIMEOUT,
  Bp_EC_PTHREAD_UNKOWN,
  Bp_EC_NOINPUT,
  Bp_EC_NOSPACE,
  Bp_EC_GET_HEAD_NULL,
  Bp_EC_TYPE_MISMATCH,
  Bp_EC_BAD_PYOBJECT,
  Bp_EC_COND_INIT_FAIL,
  Bp_EC_MUTEX_INIT_FAIL,
  Bp_EC_NULL_FILTER,
  Bp_EC_ALREADY_RUNNING,
  Bp_EC_THREAD_CREATE_FAIL,
  Bp_EC_THREAD_JOIN_FAIL,
  Bp_EC_DTYPE_MISMATCH,    /* Source/sink data types don't match */
  Bp_EC_WIDTH_MISMATCH,    /* Data width mismatch */
  Bp_EC_CAPACITY_MISMATCH, /* Data width mismatch */
  Bp_EC_DTYPE_INVALID,
  Bp_EC_INVALID_DTYPE,              /* Invalid or unsupported data type */
  Bp_EC_INVALID_CONFIG,             /* Invalid configuration parameters */
  Bp_EC_INVALID_CONFIG_WORKER,      /* Invalid configuration parameters */
  Bp_EC_INVALID_CONFIG_MAX_INPUTS,  /* Invalid configuration parameters */
  Bp_EC_INVALID_CONFIG_MAX_SINKS,   /* Invalid configuration parameters */
  Bp_EC_INVALID_CONFIG_FILTER_SIZE, /* Invalid configuration parameters */
  Bp_EC_INVALID_CONFIG_FILTER_T,    /* Invalid configuration parameters */
  Bp_EC_INVALID_CONFIG_TIMEOUT,     /* Invalid configuration parameters */
  Bp_EC_CONFIG_REQUIRED,            /* Configuration missing required fields */
  Bp_EC_MALLOC_FAIL,                /* Memory allocation failure */
  Bp_EC_MEMCPY_FAIL,                /* Memory allocation failure */
  Bp_EC_MEMSET_FAIL,                /* Memory allocation failure */
  Bp_EC_THREAD_CREATE_NAME_FAIL,    /* Failed to name thread */
  Bp_EC_BUFFER_EMPTY,
  Bp_EC_CONNECTION_OCCUPIED,
  Bp_EC_INVALID_SINK_IDX,
  Bp_EC_NULL_BUFF,
  Bp_EC_ALREADY_REGISTERED,
  Bp_EC_NOT_IMPLEMENTED,
  Bp_EC_NULL_POINTER,
  Bp_EC_NO_SINK,              /* BatchMatcher requires connected sink */
  Bp_EC_PHASE_ERROR,           /* Input has non-integer sample phase */
  Bp_EC_MAX,
} Bp_EC;

#define ERR_LUT_ENTRY(err) [Bp_EC_##err] = #err

extern char err_lut[Bp_EC_MAX][100];

typedef struct _err_info {
  Bp_EC ec;
  int line_no;
  const char *filename;
  const char *function;
  const char *err_msg;
} Err_info;

#endif /* BPIPE_CORE_H */
