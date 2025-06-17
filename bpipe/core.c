#include "core.h"
#include <stdio.h>

const size_t _data_size_lut[] = {
    [DTYPE_FLOAT] = sizeof(float),
    [DTYPE_INT] = sizeof(int),
    [DTYPE_UNSIGNED] = sizeof(unsigned),
};

void* Bp_Worker(void* filter) {
	Bp_Filter_t* f = (Bp_Filter_t*)filter;
	Bp_Batch_t input_batch = f->has_input_buffer ? Bp_head(f)       : (Bp_Batch_t){ .data = NULL, .capacity = 0, .ec=Bp_EC_NOINPUT};
	Bp_Batch_t output_batch = f->sink ? Bp_allocate(f->sink) : (Bp_Batch_t){ .data = malloc(1024 * f->data_width), .capacity = 1024 };

	while (f->running) {
		f->transform(filter, &input_batch, &output_batch);

		if (f->has_input_buffer && (input_batch.head >= input_batch.capacity)) {
			Bp_delete_tail(f);
			input_batch = Bp_head(f);
		}
		assert(output_batch.head <= output_batch.capacity);
		assert(output_batch.tail <= output_batch.capacity);
		assert(output_batch.tail <= output_batch.head);

		if (output_batch.head >= output_batch.capacity) {
			if (f->sink) {
				Bp_submit_batch(f->sink, &output_batch);
				output_batch = Bp_allocate(f->sink);
			} else {
				output_batch.head = 0;
				output_batch.tail = 0;
			}
		}
	}
	return NULL;
}



void BpPassThroughTransform(Bp_Filter_t* filt, Bp_Batch_t *input_batch, Bp_Batch_t *output_batch){
        size_t available = input_batch->head - input_batch->tail;
        size_t space     = output_batch->capacity - output_batch->head;
        size_t ncopy     = available < space ? available : space;

        if(ncopy){
                void* src = (char*)input_batch->data + input_batch->tail * filt->data_width;
                void* dst = (char*)output_batch->data + output_batch->head * filt->data_width;
                memcpy(dst, src, ncopy * filt->data_width);
        }

        output_batch->t_ns      = input_batch->t_ns;
        output_batch->period_ns = input_batch->period_ns;
        output_batch->dtype     = input_batch->dtype;
        output_batch->meta      = input_batch->meta;
        output_batch->ec        = input_batch->ec;

        input_batch->tail  += ncopy;
        output_batch->head += ncopy;
}

