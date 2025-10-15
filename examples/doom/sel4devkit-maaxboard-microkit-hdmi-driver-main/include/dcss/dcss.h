/* This work is Crown Copyright NCSC, 2024. */

#ifndef __DCSS_H__
#define __DCSS_H__

#include <vic_table.h>
#include <API_General.h>

// CCM
#define CCM_CCGR93_SET 0x45d4 // 5.1.7.7 CCM Clock Gating Register (CCM_CCGR93_SET)
#define CCM_TARGET_ROOT20 0x8a00 // 5.1.7.10 Target Register (CCM_TARGET_ROOT20)
#define CCM_TARGET_ROOT22 0x8b00 // 5.1.7.10 Target Register (CCM_TARGET_ROOT22)

// GPC
#define GPC_PGC_CPU_0_1_MAPPING 0x00EC // 5.2.10.31 PGC CPU mapping (GPC_PGC_CPU_0_1_MAPPING)
#define GPC_PU_PGC_SW_PUP_REQ 0x00F8 // 5.2.10.34 PU PGC software up trigger (GPC_PU_PGC_SW_PUP_REQ)

#define CONTROL0 0x10 // 15.2.2.1.6 Control (CONTROL0)

// DTRC
#define DTCTRL_CHAN2 0x160c8 // 15.6.6.1.28 DTRC Control (DTCTRL)
#define DTCTRL_CHAN3 0x170c8 // 15.6.6.1.28 DTRC Control (DTCTRL)
void write_dcss_memory_registers();

void init_dcss();
void reset_dcss();
void init_gpc();
void init_ccm();

#endif