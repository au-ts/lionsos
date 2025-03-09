#pragma once

#include <stdint.h>
#include "string.h"
#include <sddf/util/util.h>

struct ll_info {
    uint8_t *llnode_pool;   // pointer to region of memory used for the pool
    uint32_t pool_size;       // size of the pool, # of elements
    uint32_t node_size;       // size of each llnode, # bytes
    void *empty_head;       // head of the empty list, used to allocate from the pool
    void *head;
    void *tail;
    uint32_t curr_size;
};

// The start of each struct must include this struct.
struct llnode_ptrs {
    void *next;
    void *prev;
};

#define LLNODE_PTRS_CAST(void_ptr) ((struct llnode_ptrs *)void_ptr)

static void llinit(struct ll_info *info)
{
    assert(info);

    sddf_memset(info->llnode_pool, 0, info->pool_size * info->node_size);
    info->empty_head = info->llnode_pool;
    struct llnode_ptrs *curr = LLNODE_PTRS_CAST(info->empty_head);
    curr->prev = NULL;

    for (size_t i = 1; i < info->pool_size; i++) {
        curr->next = &info->llnode_pool[i * info->node_size];
        LLNODE_PTRS_CAST(curr->next)->prev = curr;
        curr = LLNODE_PTRS_CAST(curr->next);
    }
    curr->next = NULL;
    info->curr_size = 0;
}

static bool llfull(struct ll_info *info)
{
    return (info->empty_head == NULL);
}

static void *llalloc(struct ll_info *info)
{
    assert(info);

    if (llfull(info)) {
        return NULL;
    }

    void *ret = info->empty_head;
    info->empty_head = LLNODE_PTRS_CAST(info->empty_head)->next;

    return ret;
}

static void llfree(struct ll_info *info, void *node)
{
    assert(info && node);

    struct llnode_ptrs *prev = LLNODE_PTRS_CAST(node)->prev;
    struct llnode_ptrs *next = LLNODE_PTRS_CAST(node)->next;

    /* A -> node -> B: if A exists, A->next = B */
    if (prev) {
        prev->next = next;
    }

    /* A -> node -> B:  if B exists, B->prev = A */
    if (next) {
        next->prev = prev;
    } else {
        /* if node was the tail */
        info->tail = prev;
    }

    /* Return to free list. */
    LLNODE_PTRS_CAST(node)->next = info->empty_head;
    info->empty_head = node;

    sddf_memset(node, 0, info->node_size);
}

static void llpush(struct ll_info *info, void *node)
{
    assert(info && node);

    LLNODE_PTRS_CAST(node)->next = info->head;
    if (info->head) {
        LLNODE_PTRS_CAST(info->head)->prev = node;
    }
    info->head = node;

    if (!info->tail) {
        info->tail = node;
    }
    info->curr_size++;
}

static void *llpop(struct ll_info *info, void **new_head)
{
    assert(info);

    void *ret = info->head;
    info->head = LLNODE_PTRS_CAST(ret)->next;
    new_head = info->head;

    if (!new_head) {
        info->tail = NULL;
    }
    info->curr_size--;
    return ret;
}

static void llappend(struct ll_info *info, void *node)
{
    assert(info && node);

    if (!info->head) {
        /* Empty list */
        info->head = node;
        info->tail = node;
    } else {
        LLNODE_PTRS_CAST(info->tail)->next = node;
        LLNODE_PTRS_CAST(node)->prev = info->tail;
        info->tail = node;
    }
    info->curr_size++;
}

/**
 * Insert "node" before "right" in the linked list.
 */
static void llinsert_before(struct ll_info *info, void *right, void *node)
{
    assert(right && node);

    LLNODE_PTRS_CAST(node)->next = right;
    LLNODE_PTRS_CAST(node)->prev = LLNODE_PTRS_CAST(right)->prev;
    LLNODE_PTRS_CAST(LLNODE_PTRS_CAST(node)->prev)->next = node; // yes, this is ugly.
    LLNODE_PTRS_CAST(right)->prev = node;
    info->curr_size++;
}

static void *llpeek(struct ll_info *info)
{
    assert(info);
    return info->head;
}

/*
 * Peek the node of a given index.
 */
static void *llpeek_index(struct ll_info *info, uint32_t index)
{
    assert(info);
    if (index < info->curr_size) {
        void *curr = info->head;
        for (int i = 0; i < index; i++) {
            curr = LLNODE_PTRS_CAST(curr)->next;
        }
        return curr;
    }

    return NULL;
}