#ifndef _FRAME_TABLE_H
#define _FRAME_TABLE_H

#include <sel4/sel4.h>

/**
 * TODO: support multiple mappings.
 * - currently only one page per frame...
 */
typedef struct folio {
    struct folio *next;
    struct folio *prev;
    uint32_t frame_page;
} frame_t;

/* Memory-efficient doubly linked list of frames
 *
 * As all frame objects will live in effectively an array, we only need
 * to be able to index into that array.
 */
typedef struct {
    /* Index of first element in list */
    frame_t* first;
    /* Index in last element of list */
    frame_t* last;
    /* Size of list (useful for debugging) */
    uint64_t length;
} frame_list_t;

#endif