#include <stdint.h>
#define _DEFAULT_SOURCE
#include <unistd.h>
#include "../bpipe/batch_buffer.h"
#include "unity.h"

Batch_buff_t buff_block;
Batch_buff_t buff_drop;

#define BATCH_CAPACITY_EXPO 4
#define RING_CAPACITY_EXPO 4

char buff[124];

void setUp(void)
{
	TEST_MESSAGE("Initialising buff_block");
	TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, bb_init(&buff_block, "TEST_BUFF", DTYPE_U32, RING_CAPACITY_EXPO, BATCH_CAPACITY_EXPO, OVERFLOW_BLOCK), "Failed to init buff_block");
	TEST_MESSAGE("Initialising buff_drop");
	TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, bb_init(&buff_drop, "TEST_BUFF", DTYPE_U32, RING_CAPACITY_EXPO, BATCH_CAPACITY_EXPO, OVERFLOW_DROP), "Failed to init buff_drop");
}

void tearDown(void)
{
	TEST_MESSAGE("De-Initialising buff_drop");
	TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, bb_deinit(&buff_block), "Failed to de-init buff_block");
	TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, bb_deinit(&buff_drop), "Failed to de-init buff_drop");
}


/* Fill the buffer to cappacity with a ramp waveform incrementing by one every sample */
void test_fill_and_empty(void)
{
	TEST_MESSAGE("Filling buff_block");
	size_t ring_capacity = (1 << RING_CAPACITY_EXPO) -1; // TODO: check this, due to the "always occupied slot
	size_t buff_capacity = (1 << BATCH_CAPACITY_EXPO); // 
	TEST_MESSAGE("Before filling");

	bb_print(&buff_block);

	snprintf(buff, sizeof(buff), "Ring Cappacity=%lu, Buff Capacity=%lu", ring_capacity, buff_capacity);
	TEST_MESSAGE(buff);
	uint32_t count = 0;
	for (int i = 0; i<ring_capacity; i++){
		Batch_t* batch = bb_get_head(&buff_block);
		batch->t_ns = i * 1000000;  // Convert to nanoseconds for better display
		batch->period_ns = 2;
		batch->batch_id= i;
		uint32_t* data = (uint32_t*)buff_block.data_ring + (bb_get_head_idx(&buff_block) * buff_capacity);
		batch->data = data;
		for (int ii = 0; ii<buff_capacity; ii++){
			data[ii] = count++;
		}
		// Submit the batch to advance the head pointer
		Bp_EC rc = bb_submit(&buff_block, 0);
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
		TEST_ASSERT_NOT_NULL_MESSAGE(batch, "Failed to get tail batch");
		TEST_ASSERT_EQUAL_INT_MESSAGE(i, batch->batch_id, "Batch ID not incrementing linearly.") ;
		TEST_ASSERT_EQUAL_INT_MESSAGE(2, batch->period_ns, "Batch period is unexpected value.") ;
		TEST_ASSERT_EQUAL_INT_MESSAGE(i * 1000000, batch->t_ns, "Batch timestamp not incrementing linearly.") ;
		
		// Verify data
		uint32_t* data = (uint32_t*)batch->data;
		for (int ii = 0; ii<buff_capacity; ii++){
			TEST_ASSERT_EQUAL_INT_MESSAGE(count, data[ii], "Batch Data is not incrementing linearly.") ;
			count++;
		}
		
		// Delete the batch to advance tail
		Bp_EC rc = bb_del(&buff_block);
		TEST_ASSERT_EQUAL_INT_MESSAGE(Bp_EC_OK, rc, "Failed to delete batch");
	}
	
	TEST_MESSAGE("After consuming all batches");
	bb_print(&buff_block);

}

void test_overflow_drop(void)
{
}

void test_block_timeout(void)
{
}

int main(int argc, char* argv[])
{
    UNITY_BEGIN();
		RUN_TEST(test_fill_and_empty);
    return UNITY_END();
}
