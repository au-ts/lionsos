#include <stdbool.h>
#include <stdint.h>

#include <fs/protocol.h>

bool sddf_fs_queue_push(struct sddf_fs_queue *queue, union sddf_fs_message message) {
    if (queue->size == SDDF_FS_QUEUE_CAPACITY) {
        return false;
    }
    queue->buffer[queue->write_index] = message;
    queue->write_index = (queue->write_index + 1) % SDDF_FS_QUEUE_CAPACITY;
    queue->size++;
    return true;
}

bool sddf_fs_queue_pop(struct sddf_fs_queue *queue, union sddf_fs_message *message) {
    if (queue->size == 0) {
        return false;
    }
    *message = queue->buffer[queue->read_index];
    queue->read_index = (queue->read_index + 1) % SDDF_FS_QUEUE_CAPACITY;
    queue->size--;
    return true;
}
