/*
 * Copyright 2017, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * Copyright 2019 Adventium Labs
 * Modifications made to original
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_Adventium_BSD)
 */

#include <sb_queue_int32_t_1.h>

//------------------------------------------------------------------------------
// Sender API
//
// See sb_queue_int32_t_1.h for API documentation. Only implementation details are documented here.

void sb_queue_int32_t_1_init(sb_queue_int32_t_1_t *queue) {
  // NOOP for now. C's struct initialization is sufficient.  If we ever do need
  // initialization logic, we may also need to synchronize with receiver
  // startup.
}

void sb_queue_int32_t_1_enqueue(
  sb_queue_int32_t_1_t *queue,
  int32_t *data) {

  // Simple ring with one dirty element that will be written next. Only one
  // writer, so no need for any synchronization.
  // elt[queue->numSent % SB_QUEUE_INT32_T_1_SIZE]
  // is always considered dirty. So do not advance queue->NumSent
  // till AFTER data is copied.

  size_t index = queue->numSent % SB_QUEUE_INT32_T_1_SIZE;

  queue->elt[index] = *data; // Copy data into queue

  // Release memory fence - ensure that data write above completes BEFORE we advance queue->numSent
  __atomic_thread_fence(__ATOMIC_RELEASE);

  ++(queue->numSent);
}

//------------------------------------------------------------------------------
// Receiver API
//
// See sb_queue_int32_t_1.h for API documentation. Only implementation details are documented here.

void sb_queue_int32_t_1_Recv_init(
  sb_queue_int32_t_1_Recv_t *recvQueue,
  sb_queue_int32_t_1_t *queue) {

  recvQueue->numRecv = 0;
  recvQueue->queue = queue;
}

bool sb_queue_int32_t_1_dequeue(
  sb_queue_int32_t_1_Recv_t *recvQueue,
  sb_event_counter_t *numDropped,
  int32_t *data) {

  sb_event_counter_t *numRecv = &recvQueue->numRecv;
  sb_queue_int32_t_1_t *queue = recvQueue->queue;

  // Get a copy of numSent so we can see if it changes during read
  sb_event_counter_t numSent = queue->numSent;

  // Acquire memory fence - ensure read of queue->numSent BEFORE reading data
  __atomic_thread_fence(__ATOMIC_ACQUIRE);

  // How many new elements have been sent? Since we are using unsigned
  // integers, this correctly computes the value as counters wrap.
  sb_event_counter_t numNew = numSent - *numRecv;
  if (0 == numNew) {
    // Queue is empty
    return false;
  }

  // One element in the ring buffer is always considered dirty. Its the next
  // element we will write.  It's not safe to read it until numSent has been
  // incremented. Thus there are really only (SB_QUEUE_INT32_T_1_SIZE - 1)
  // elements in the queue.
  *numDropped = (numNew <= SB_QUEUE_INT32_T_1_SIZE - 1) ? 0 : numNew - SB_QUEUE_INT32_T_1_SIZE + 1;

  // Increment numRecv by *numDropped plus one for the element we are about to read.
  *numRecv += *numDropped + 1;

  // UNUSED - number of elements left to be consumed
  //sb_event_counter_t numRemaining = numSent - *numRecv;

  size_t index = (*numRecv - 1) % SB_QUEUE_INT32_T_1_SIZE;
  *data = queue->elt[index]; // Copy data

  // Acquire memory fence - ensure read of data BEFORE reading queue->numSent again
  __atomic_thread_fence(__ATOMIC_ACQUIRE);

  if (queue->numSent - *numRecv + 1 < SB_QUEUE_INT32_T_1_SIZE) {
    // Sender did not write element we were reading. Copied data is coherent.
    return true;
  } else {
    // Sender may have written element we were reading. Copied data may be incoherent.
    // We dropped the element we were trying to read, so increment *numDropped.
    ++(*numDropped);
    return false;
  }
}

bool sb_queue_int32_t_1_is_empty(sb_queue_int32_t_1_Recv_t *recvQueue) {
  return (recvQueue->queue->numSent == recvQueue->numRecv);
}