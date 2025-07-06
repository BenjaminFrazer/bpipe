#include "core.h"
#include "bperr.h"
#include <string.h>
#define _GNU_SOURCE             /* See feature_test_macros(7) */
#include <pthread.h>

/* Configuration-based initialization API */
Bp_EC filt_init(Filter_t* f, Core_filt_config_t config)
{
	if (f==NULL){
		return Bp_EC_NULL_FILTER;
	}

	if (config.timeout_us < 0){
		return Bp_EC_INVALID_CONFIG;
	}
	f->timeout_us = config.timeout_us;

	if (config.max_supported_sinks> MAX_SINKS){
		return Bp_EC_INVALID_CONFIG;
	}
	f->max_suppported_sinks = config.max_supported_sinks;

	if (config.n_inputs > MAX_INPUTS){
		return Bp_EC_INVALID_CONFIG;
	}
	f->n_input_buffers = config.n_inputs;

	for (int i = 0; i<config.n_inputs; i++){
		Bp_EC rc = bb_init(&f->input_buffers[i], "NDEF", config.buff_config);
		if (rc != Bp_EC_OK){
			return rc;
		}
	}

	if (config.name != NULL){
		strncpy(f->name, config.name, sizeof(f->name));
	}
	else{
		strncpy(f->name, "NDEF", sizeof(f->name));
	}
	return Bp_EC_OK;
}

Bp_EC filt_deinit(Filter_t *f)
{
	for (int i = 0; i<f->n_input_buffers; i++){
		Bp_EC rc = bb_deinit(&f->input_buffers[i]);
		if (rc != Bp_EC_OK){
			return rc;
		}
	}
	return Bp_EC_OK;
}

/* Multi-I/O connection functions */
Bp_EC filt_connectsink(Filter_t *f, size_t sink_idx, Batch_buff_t *dest_buffer)
{
	if (f==NULL){
		return Bp_EC_NULL_FILTER;
	}
	if (dest_buffer==NULL){
		return Bp_EC_NULL_BUFF;
	}
	if (sink_idx>MAX_SINKS){
		return Bp_EC_INVALID_SINK_IDX;
	}
	if (f->sinks[sink_idx] != NULL){
		return Bp_EC_CONNECTION_OCCUPIED;
	}

	return Bp_EC_OK;
}

Bp_EC filt_detatchsink(Filter_t *f, size_t sink_idx)
{
	if (f==NULL){
		return Bp_EC_NULL_FILTER;
	}
	if (sink_idx>MAX_SINKS){
		return Bp_EC_INVALID_SINK_IDX;
	}
	f->sinks[sink_idx] = NULL;
	return Bp_EC_OK;
}

/* Filter lifecycle functions */
Bp_EC filt_start(Filter_t *f)

{
	if (!f) {
		return Bp_EC_NULL_FILTER;
	}
	if (f->running) {
		return Bp_EC_ALREADY_RUNNING;
	}

	f->running = true;

	if (pthread_create(&f->worker_thread, NULL, f->worker,
										(void *) f) != 0) {
		f->running = false;
		return Bp_EC_THREAD_CREATE_FAIL;
	}
	//if (pthread_setname_np(f->worker_thread, f->name)) {
	//	return Bp_EC_THREAD_CREATE_NAME_FAIL;
	//}
	return Bp_EC_OK;
}

Bp_EC filt_stop(Filter_t *f)
{
	if (!f) {
		return Bp_EC_NULL_FILTER;
	}

	if (!f->running) {
		return Bp_EC_OK;  // Already stopped, not an error
	}

	f->running = false;

	// Stop all input buffers to wake up any waiting threads
	for (int i = 0; i < MAX_INPUTS; i++) {
		if (f->input_buffers[i].data_ring != NULL) {
			bb_stop(&f->input_buffers[i]);
		}
	}

	if (pthread_join(f->worker_thread, NULL) != 0) {
		return Bp_EC_THREAD_JOIN_FAIL;
	}

	return Bp_EC_OK;
}
