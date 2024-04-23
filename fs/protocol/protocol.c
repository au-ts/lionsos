#include <stdbool.h>
#include <stdint.h>

#include <fs/protocol.h>

bool sddf_fs_queue_push(struct sddf_fs_queue *queue, union sddf_fs_message message) {
    uint64_t tail = queue->tail;
    if (tail + 1 == queue->head) {
        return false;
    }
    queue->buffer[tail % SDDF_FS_QUEUE_CAPACITY] = message;
    queue->tail = tail + 1;
    return true;
}

bool sddf_fs_queue_pop(struct sddf_fs_queue *queue, union sddf_fs_message *message) {
    uint64_t head = queue->head;
    if (head == queue->tail) {
        return false;
    }
    *message = queue->buffer[head % SDDF_FS_QUEUE_CAPACITY];
    queue->head = head + 1;
    return true;
}
