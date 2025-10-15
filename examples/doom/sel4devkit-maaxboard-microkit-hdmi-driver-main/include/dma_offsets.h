/* This work is Crown Copyright NCSC, 2024. */

#ifndef __DMA_OFFSETS_H__
#define __DMA_OFFSETS_H__

#define FRAME_BUFFER_SIZE 2000 * 1080 * 4                                   // (1920 * 1080 * 4) with some padding. 
#define FRAME_BUFFER_ONE_OFFSET 0                                           // The first frame buffer is at position 0 in the dma pool
#define FRAME_BUFFER_TWO_OFFSET FRAME_BUFFER_SIZE                           // Second frame buffer starts at the end of the first
#define CTX_LD_DB_ONE_ADDR FRAME_BUFFER_SIZE * 2                           // First Context load dma memory region starts at the end of the second frame buffer
#define CTX_LD_DB_TWO_ADDR (FRAME_BUFFER_SIZE * 2) + 100000                 // Second Context load dma memory region starts at the end of the second frame buffer with an offset
#define ACTIVE_FRAME_BUFFER_ADDR_OFFSET (FRAME_BUFFER_SIZE * 2) + 200000    // Used to store the current offset to be applied to virtual memory to access the current frame buffer
#define CACHE_FRAME_BUFFER_ADDR_OFFSET (FRAME_BUFFER_SIZE * 2) + 300000     // Used to store the current offset to be applied to virtual memory to access the current frame buffer
#define DMA_SIZE FRAME_BUFFER_SIZE * 3                                      // Total size of the dma 

#endif