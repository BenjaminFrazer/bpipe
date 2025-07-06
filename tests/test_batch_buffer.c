#include "unity_internals.h"
#include <bits/time.h>
#include <bits/types/struct_iovec.h>
#include <bits/types/timer_t.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#define _DEFAULT_SOURCE
#include <unistd.h>
#include "../bpipe/batch_buffer.h"
#include "unity.h"

Batch_buff_t buff_block;
Batch_buff_t buff_drop;

#define BATCH_CAPACITY_EXPO 4
#define RING_CAPACITY_EXPO 4

char buff[124];

size_t ring_capacity = (1 << RING_CAPACITY_EXPO) -1; // TODO: check this, due to the "always occupied slot
size_t batch_capacity = (1 << BATCH_CAPACITY_EXPO); // 

void setUp(void)
{
	TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, bb_init(&buff_block, "TEST_BUFF", DTYPE_U32, RING_CAPACITY_EXPO, BATCH_CAPACITY_EXPO, OVERFLOW_BLOCK), "Failed to init buff_block");
	TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, bb_init(&buff_drop, "TEST_BUFF", DTYPE_U32, RING_CAPACITY_EXPO, BATCH_CAPACITY_EXPO, OVERFLOW_DROP), "Failed to init buff_drop");
	TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, bb_start(&buff_block), "Failed to start buff_block");
	TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, bb_start(&buff_drop), "Failed to start buff_drop");
}

void tearDown(void)
{
	TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, bb_stop(&buff_block), "Failed to stop buff_block");
	TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, bb_stop(&buff_drop), "Failed to stop buff_drop");
	TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, bb_deinit(&buff_block), "Failed to de-init buff_block");
	TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, bb_deinit(&buff_drop), "Failed to de-init buff_drop");
}


/* Fill the buffer to cappacity with a ramp waveform incrementing by one every sample */
void test_fill_and_empty(void)
{
	TEST_MESSAGE("Filling buff_block");
	TEST_MESSAGE("Before filling");

	bb_print(&buff_block);

	snprintf(buff, sizeof(buff), "Ring Cappacity=%lu, Buff Capacity=%lu", ring_capacity, batch_capacity);
	TEST_MESSAGE(buff);
	uint32_t count = 0;
	for (int i = 0; i<ring_capacity; i++){
		Batch_t* batch = bb_get_head(&buff_block);
		batch->t_ns = i * 1000000;  // Convert to nanoseconds for better display
		batch->period_ns = 2;
		batch->batch_id= i;
		TEST_ASSERT_EQUAL_PTR_MESSAGE((char*)buff_block.data_ring + (batch_capacity * sizeof(uint32_t) * i), batch->data, "Batch data pointer in unexpected location.");
		for (int ii = 0; ii<batch_capacity; ii++){
			uint32_t* data = BATCH_GET_SAMPLE_U32(batch, ii);
			*data = count++;
		}
		// Submit the batch to advance the head pointer
		Bp_EC rc = bb_submit(&buff_block, 10000);  // 10ms timeout
		TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, rc, "Failed to submit batch");
	}

	TEST_MESSAGE("After filling");
	bb_print(&buff_block);

	TEST_MESSAGE("Consuming batches");
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, bb_get_tail_idx(&buff_block), "Unexpected tail index");
	
	// Reset count for verification
	count = 0;
	for (int i = 0; i<ring_capacity; i++){
		Batch_t* batch = bb_get_tail(&buff_block, 0);
		TEST_ASSERT_EQUAL_PTR_MESSAGE((char*)buff_block.data_ring + (batch_capacity * sizeof(uint32_t) * i), batch->data, "Batch data pointer in unexpected location.");

		TEST_ASSERT_NOT_NULL_MESSAGE(batch, "Failed to get tail batch");
		TEST_ASSERT_EQUAL_INT_MESSAGE(i, batch->batch_id, "Batch ID not incrementing linearly.") ;
		TEST_ASSERT_EQUAL_INT_MESSAGE(2, batch->period_ns, "Batch period is unexpected value.") ;
		TEST_ASSERT_EQUAL_INT_MESSAGE(i * 1000000, batch->t_ns, "Batch timestamp not incrementing linearly.") ;
		
		// Verify data
		for (int ii = 0; ii<batch_capacity; ii++){
			uint32_t* data = BATCH_GET_SAMPLE_U32(batch, ii);
			TEST_ASSERT_EQUAL_INT_MESSAGE(count, *data, "Batch Data is not incrementing linearly.") ;
			count++;
		}
		// Delete the batch to advance tail
		Bp_EC rc = bb_del(&buff_block);
		TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, rc, "Failed to delete batch");
	}
	
	TEST_MESSAGE("After consuming all batches");
	bb_print(&buff_block);

}

void * submitter(void *arg){
	Bp_EC* ec = (Bp_EC*)arg;
	*ec = bb_submit(&buff_block, 1000000);
	return (void*)ec;
}

/* */
void test_overflow_block(void)
{
	Batch_t* batch;
	size_t count = 0;
	for (int i = 0; i<ring_capacity; i++){
		batch = bb_get_head(&buff_block);
		batch->t_ns = i * 1000000;  // Convert to nanoseconds for better display
		batch->period_ns = 2;
		batch->batch_id= i;
		for (int ii = 0; ii<batch_capacity; ii++){
			uint32_t* data = BATCH_GET_SAMPLE_U32(batch, ii);
			*data = count++;
		}
		// Submit the batch to advance the head pointer
		Bp_EC rc = bb_submit(&buff_block, 10000);  // 10ms timeout
		TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, rc, "Failed to submit batch");
	}
	
	/* This slot should be accessible but not possible to submit */
	batch = bb_get_head(&buff_block);
	
	/* Try a 5ms timeout */
	Bp_EC ec;
	long long ts_before = now_ns(CLOCK_MONOTONIC);
	ec = bb_submit(&buff_block, 5000);
	long long ts_after = now_ns(CLOCK_MONOTONIC);
	long long elapsed_ns = ts_after - ts_before;
	
	TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_TIMEOUT, ec, "Expected timeout fail.");
	/* Timeout should be at least 4ms (allowing 1ms tolerance for system scheduling) */
	TEST_ASSERT_GREATER_THAN_INT_MESSAGE(4000000, elapsed_ns, "Timeout was shorter than 4ms");
	/* But not longer than 10ms */
	TEST_ASSERT_LESS_THAN_INT_MESSAGE(10000000, elapsed_ns, "Timeout took longer than 10ms");


	/* test that stopping the batch buffer imediately unblocks */

	/* Bock for 1s */
	pthread_t test_blocked_submitter;
	
	Bp_EC submitter_ec;
	void* ec_ptr = (void*)&submitter_ec;
	ts_before = now_ns(CLOCK_MONOTONIC);
	pthread_create(&test_blocked_submitter, NULL, submitter, ec_ptr);

	struct timespec sleeptime = {.tv_nsec=10000000}; // 10ms
	nanosleep(&sleeptime, NULL);
	
	ec = bb_stop(&buff_block);
	TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, ec, "Failed to stop.");

	TEST_ASSERT_EQUAL_INT_MESSAGE(0, pthread_join(test_blocked_submitter, &ec_ptr), "Failed to join");
	ts_after = now_ns(CLOCK_MONOTONIC);
	elapsed_ns = ts_after - ts_before;

	ec = *((Bp_EC*)ec_ptr);
	TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_STOPPED, ec, "Expected timeout fail.");

	/* Timeout should be at least 10ms */
	TEST_ASSERT_GREATER_THAN_INT_MESSAGE(10000000, elapsed_ns, "Join quicker than expected.");
	/* But not longer than 20ms */
	TEST_ASSERT_LESS_THAN_INT_MESSAGE(12000000, elapsed_ns, "Join slower than expected. ");

}

int main(int argc, char* argv[])
{
    UNITY_BEGIN();
		RUN_TEST(test_fill_and_empty);
		RUN_TEST(test_overflow_block);
    return UNITY_END();
}
