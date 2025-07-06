#include "map.h"
#include "batch_buffer.h"
#include "bperr.h"
#include "core.h"
#include <bits/types/struct_iovec.h>
#include <stdbool.h>
#include <time.h>


void* map_worker(void* arg){
	Map_filt_t* f = (Map_filt_t*)arg;
	Batch_t *input_batch = NULL, *output_batch = NULL;
	Bp_EC err = Bp_EC_OK;
	
	// Validate configuration
	if (!f->base.sinks[0] || !f->map_fcn) {
		f->base.worker_err_info.ec = Bp_EC_INVALID_CONFIG;
		return NULL;
	}
	
	// Check data types match
	SampleDtype_t input_type = f->base.input_buffers[0].dtype;
	SampleDtype_t output_type = f->base.sinks[0]->dtype;
	
	if (input_type != output_type) {
		f->base.worker_err_info.ec = Bp_EC_DTYPE_MISMATCH;
		return NULL;
	}
	
	if (input_type == DTYPE_NDEF || input_type >= DTYPE_MAX) {
		f->base.worker_err_info.ec = Bp_EC_INVALID_DTYPE;
		return NULL;
	}
	
	size_t data_width = bb_getdatawidth(input_type);
	size_t batch_size = bb_batch_size(f->base.sinks[0]);
	
	// Main processing loop
	while (atomic_load(&f->base.running)) {
		// Manage input batch
		if (!input_batch || input_batch->tail >= input_batch->head) {
			if (input_batch) {
				err = bb_del(&f->base.input_buffers[0]);
				if (err != Bp_EC_OK) break;
			}
			
			input_batch = bb_get_tail(&f->base.input_buffers[0], f->base.timeout_us, &err);
			if (!input_batch) break;  // Timeout or stopped
		}
		
		// Manage output batch
		if (!output_batch || output_batch->head >= batch_size) {
			if (output_batch) {
				err = bb_submit(f->base.sinks[0], f->base.timeout_us);
				if (err != Bp_EC_OK) break;
			}
			
			output_batch = bb_get_head(f->base.sinks[0]);
			output_batch->head = 0;  // Initialize write position
		}
		
		// Process data
		size_t input_available = input_batch->head - input_batch->tail;
		size_t output_available = batch_size - output_batch->head;
		size_t n = (input_available < output_available) ? input_available : output_available;
		
		if (n == 0) continue;  // Shouldn't happen, but be safe
		
		// Call map function with correct pointers
		err = f->map_fcn(
			output_batch->data + output_batch->head * data_width,
			input_batch->data + input_batch->tail * data_width,
			n
		);
		
		if (err != Bp_EC_OK) break;
		
		// Update positions
		input_batch->tail += n;
		output_batch->head += n;
	}
	
	// Set error code if we exited due to error (not just stopped)
	if (err != Bp_EC_OK && err != Bp_EC_STOPPED) {
		f->base.worker_err_info.ec = err;
	}
	
	// Submit any remaining output
	if (output_batch && output_batch->head > 0) {
		bb_submit(f->base.sinks[0], 0);  // Don't wait on cleanup
	}
	
	return NULL;
}

Bp_EC map_init(Map_filt_t* f, Map_config_t config){
	Core_filt_config_t core_config;
	if (f == NULL){
		return Bp_EC_INVALID_CONFIG;
	}

	/* copy Batch Buffer config */
	core_config.buff_config = config.buff_config;
	f->base.worker = &map_worker;

	/* Map is always a 1->1 filter */
	core_config.n_inputs = 1;
	core_config.max_supported_sinks = 1;
	core_config.filt_type = FILT_T_MAP;
	core_config.size = sizeof(Map_filt_t);

	Bp_EC err = filt_init(&f->base, core_config);	

	if (err != Bp_EC_OK){
		return err;
	}
	if (config.map_fcn == NULL){
		return Bp_EC_INVALID_CONFIG;
	}
	f->map_fcn = config.map_fcn;

	return Bp_EC_OK;
};
