/*
 * Copyright 2026, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef _PAGER_H
#define _PAGER_H

#include <stdint.h>

/* Maximum number of children the pager can page for. */
#define MAX_CHILDREN 10

/*
 * Slab tuning, shared by the frame, global zero page and intermediary paging
 * structure free lists: how many objects a free list holds and how many are
 * retyped at a time when one runs dry.
 */
#define BUFFERS_SIZE 200000
#define REFILL_SIZE 20000

/*
 * Slots of the CNodes the Microkit tool mapped into the pager's root CSpace.
 * These must match PAGER_CNODES in meta.py.
 */
#define UNTYPED_SLOT 1  // all untyped memory left after initialisation.
#define FRAME_CNODE 2   // where frame caps are placed
#define IPS_CNODE 3     // where intermediary paging structure caps are placed.
#define GZP_CNODE 4     // where global zero frame caps are placed.
#define PROCESS_CNODES 5 // where the CSpaces created by fork() are placed.
#define ELF_CAPS 6      // the children's ELF frames, filled in by the tool.

/*
 * Symbols the Microkit tool patches into pager.elf. pager_memory and
 * remaining_untypeds_vaddr come from the setvar_vaddr maps in meta.py, vspaces
 * is written by the capDL builder and maps a child id to its VSpace cap.
 */
extern uintptr_t pager_memory;
extern uintptr_t remaining_untypeds_vaddr;
extern uint32_t vspaces[MAX_CHILDREN];

#endif
