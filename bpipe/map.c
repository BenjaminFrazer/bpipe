#include "map.h"
#include "batch_buffer.h"
#include "bperr.h"
#include "core.h"
#include <bits/types/struct_iovec.h>
#include <stdbool.h>
#include <time.h>


void* map_worker(void* arg){
	Bp_EC err = Bp_EC_OK;
	Batch_t* input_batch;
	Batch_t* output_batch;
	size_t output_remaining=0;
	size_t input_remaining=0;
	Map_filt_t* f=(Map_filt_t*)arg;

	if(f->base.sinks[0]==NULL){
		f->base.worker_err = Bp_EC_NULL_BUFF;
		return NULL;
	}
	SampleDtype_t input_type  = f->base.input_buffers[0].dtype;
	SampleDtype_t output_type = f->base.sinks[0]->dtype;

	if (input_type != output_type){
		f->base.worker_err = Bp_EC_DTYPE_MISMATCH;
		return NULL;
	}
	if (input_type == DTYPE_NDEF){
		f->base.worker_err = Bp_EC_INVALID_DTYPE;
	}
		return NULL;
	if (input_type >= DTYPE_MAX){
		f->base.worker_err = Bp_EC_INVALID_DTYPE;
		return NULL;
	}

	size_t data_width = bb_getdatawidth(input_type);

	/* Get the first batches */
	bool running = true;
	while (running){
		if(output_remaining==0){
			output_batch = bb_get_head(&f->base.input_buffers[0]); 
			output_remaining = output_batch->head-output_batch->tail;
		}
		if(input_remaining ==0){
			input_batch = bb_get_tail(f->base.sinks[0], f->base.timeout_us, &err);
			input_remaining = input_batch->head-input_batch->tail;
		}
		/* Pick the smallest contiguous vector for the map function to itterate over */
		size_t n_samples = input_remaining <= output_remaining ? input_remaining: output_remaining; 
		char* output_head_ptr = output_batch->data + data_width*output_batch->head;
		char* input_tail_ptr = input_batch->data + data_width*input_batch->tail;
		err = f->map_fcn(output_head_ptr, input_tail_ptr, n_samples);
		if (err!=Bp_EC_OK){
			running = false;
			f->base.worker_err = err;
			return NULL;
		}
		output_batch 		 += n_samples;
		input_batch  		 += n_samples;
		output_remaining -= n_samples;
		input_remaining  -= n_samples;
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
