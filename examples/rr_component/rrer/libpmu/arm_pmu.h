#pragma once
#include <stdint.h>

// Event definitions for ARMv8 PMU
#define SW_INCR 0x00                /* Software Increment*/
#define L1I_CACHE_REFILL 0x01       /* L1 Instruction cache refill */
#define L1I_TLB_REFILL 0x02         /* L1 Instruction TLB refill */
#define L1D_CACHE_REFILL 0x03       /* L1 Data cache refill */
#define L1D_CACHE 0x04              /* L1 Data cache access */
#define L1D_TLB_REFILL 0x05         /* L1 Data TLB refill */
#define LD_RETIRED 0x06             /* Instruction architecturally executed, condition check pass - load */
#define ST_RETIRED 0x07             /* Instruction architecturally executed, condition check pass - store */
#define INST_RETIRED 0x08           /* Instruction architecturally executed */
#define EXC_TAKEN 0x09              /* Exception taken */
#define EXC_RETURN 0x0a             /* Exception returned */
#define CID_WRITE_RETIRED 0x0b      /* Change to Context ID retired */
#define PC_WRITE_RETIRED 0x0c       /* Instruction architecturally executed, condition check pass - write to CONTEXTIDR */
#define BR_IMMED_RETIRED 0x0d       /* Instruction architecturally executed, condition check pass - software change of the PC */
#define UNALIGNED_LDST_RETIRED 0x0f /* Instruction architecturally executed, condition check pass, prodcedure return */
#define BR_MIS_PRED 0x10            /* Mispredicted or not predicted branch speculatively executed */
#define CPU_CYCLES 0x11             /* Cycle */
#define BR_PRED 0x12                /* Predictable branch speculatively executed */
#define MEM_ACCESS 0x13             /* L1 Data cache access */
#define L1I_CACHE 0x14              /* L1 Instruction cache access */
#define L1D_CACHE_WB 0x15           /* L1 Data cache Write-back */
#define L2D_CACHE 0x16              /* L2 Data cache access */
#define L2D_CACHE_REFILL 0x17       /* L2 Data cache refill */
#define L2D_CACHE_WB 0x18           /* L2 Data cache write-back */
#define BUS_ACCESS 0x19             /* Bus access */
#define MEMORY_ACCESS 0x1a          /* Local memory error */
#define BUS_CYCLES 0x1d             /* Bus cycle */
#define CHAIN 0x1e                  /* Odd performance counter chain mode */
#define BUS_ACCESS_LD 0x60          /* Bus access - Read */

/* Assumes that the event ID's do not reach this high on any platform */
#define CYCLE_COUNTER UINT32_MAX