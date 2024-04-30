#include <stdbool.h>
#include <stdint.h>

#include <fs/protocol.h>

bool sddf_fs_queue_push(struct sddf_fs_queue *queue, union sddf_fs_message message) {
    if (queue->tail + 1 == __atomic_load_n(&queue->head, __ATOMIC_ACQUIRE)) {
        return false;
    }
    queue->buffer[queue->tail % SDDF_FS_QUEUE_CAPACITY] = message;
    __atomic_store_n(&queue->tail, queue->tail + 1, __ATOMIC_RELEASE);
    return true;
}

bool sddf_fs_queue_pop(struct sddf_fs_queue *queue, union sddf_fs_message *message) {
    if (queue->head == __atomic_load_n(&queue->tail, __ATOMIC_ACQUIRE)) {
        return false;
    }
    *message = queue->buffer[queue->head % SDDF_FS_QUEUE_CAPACITY];
    __atomic_store_n(&queue->head, queue->head + 1, __ATOMIC_RELEASE);
    return true;
}
