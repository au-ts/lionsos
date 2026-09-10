#pragma once
/*
 * Copyright 2021, Breakaway Consulting Pty. Ltd.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
/*
 * The Microkit Monitor.
 *
 * The monitor is the highest priority Protection Domain
 * exclusively in a Microkit system. It fulfills one purpose:
 *
 *   Acting as the fault handler for protection domains.
 */

#include <stdbool.h>
#include <stdint.h>
#include <sel4/sel4.h>

#include "base.h"
#include "microkit.h"
#include "sel4/shared_types_gen.h"
#include "sddf/util/printf.h"

#define MAX_VMS 64
#define MAX_PDS 64
#define MAX_NAME_LEN 64

#define BASE_SCHED_CONTEXT_CAP 138
#define BASE_NOTIFICATION_CAP 202

#define MASK(n) (BIT(n) - 1ULL)
#define FAULT(...) do {sddf_printf("%s FAULT| ", microkit_name); sddf_printf(__VA_ARGS__);} while (0)

static char *fault_ec_to_string(uintptr_t ec)
{
    switch (ec) {
    case 0:
        return "Unknown reason";
    case 1:
        return "Trapped WFI or WFE instruction execution";
    case 3:
        return "Trapped MCR or MRC access with (coproc==0b1111) this is not reported using EC 0b000000";
    case 4:
        return "Trapped MCRR or MRRC access with (coproc==0b1111) this is not reported using EC 0b000000";
    case 5:
        return "Trapped MCR or MRC access with (coproc==0b1110)";
    case 6:
        return "Trapped LDC or STC access";
    case 7:
        return "Access to SVC, Advanced SIMD or floating-point functionality trapped";
    case 12:
        return "Trapped MRRC access with (coproc==0b1110)";
    case 13:
        return "Branch Target Exception";
    case 17:
        return "SVC instruction execution in AArch32 state";
    case 21:
        return "SVC instruction execution in AArch64 state";
    case 24:
        return "Trapped MSR, MRS or System instruction exuection in AArch64 state, this is not reported using EC "
               "0xb000000, 0b000001 or 0b000111";
    case 25:
        return "Access to SVE functionality trapped";
    case 28:
        return "Exception from a Pointer Authentication instruction authentication failure";
    case 32:
        return "Instruction Abort from a lower Exception level";
    case 33:
        return "Instruction Abort taken without a change in Exception level";
    case 34:
        return "PC alignment fault exception";
    case 36:
        return "Data Abort from a lower Exception level";
    case 37:
        return "Data Abort taken without a change in Exception level";
    case 38:
        return "SP alignment faultr exception";
    case 40:
        return "Trapped floating-point exception taken from AArch32 state";
    case 44:
        return "Trapped floating-point exception taken from AArch64 state";
    case 47:
        return "SError interrupt";
    case 48:
        return "Breakpoint exception from a lower Exception level";
    case 49:
        return "Breakpoint exception taken without a change in Exception level";
    case 50:
        return "Software Step exception from a lower Exception level";
    case 51:
        return "Software Step exception taken without a change in Exception level";
    case 52:
        return "Watchpoint exception from a lower Exception level";
    case 53:
        return "Watchpoint exception taken without a change in Exception level";
    case 56:
        return "BKPT instruction execution in AArch32 state";
    case 60:
        return "BRK instruction execution in AArch64 state";
    }
    return "<invalid EC>";
}

static char *fault_data_abort_dfsc_to_string(uintptr_t dfsc)
{
    switch (dfsc) {
    case 0x00:
        return "address size fault, level 0";
    case 0x01:
        return "address size fault, level 1";
    case 0x02:
        return "address size fault, level 2";
    case 0x03:
        return "address size fault, level 3";
    case 0x04:
        return "translation fault, level 0";
    case 0x05:
        return "translation fault, level 1";
    case 0x06:
        return "translation fault, level 2";
    case 0x07:
        return "translation fault, level 3";
    case 0x09:
        return "access flag fault, level 1";
    case 0x0a:
        return "access flag fault, level 2";
    case 0x0b:
        return "access flag fault, level 3";
    case 0x0d:
        return "permission fault, level 1";
    case 0x0e:
        return "permission fault, level 2";
    case 0x0f:
        return "permission fault, level 3";
    case 0x10:
        return "synchronuos external abort";
    case 0x11:
        return "synchronous tag check fault";
    case 0x14:
        return "synchronous external abort, level 0";
    case 0x15:
        return "synchronous external abort, level 1";
    case 0x16:
        return "synchronous external abort, level 2";
    case 0x17:
        return "synchronous external abort, level 3";
    case 0x18:
        return "synchronous parity or ECC error";
    case 0x1c:
        return "synchronous parity or ECC error, level 0";
    case 0x1d:
        return "synchronous parity or ECC error, level 1";
    case 0x1e:
        return "synchronous parity or ECC error, level 2";
    case 0x1f:
        return "synchronous parity or ECC error, level 3";
    case 0x21:
        return "alignment fault";
    case 0x30:
        return "tlb conflict abort";
    case 0x31:
        return "unsupported atomic hardware update fault";
    }
    return "<unexpected DFSC>";
}

static char *fault_page_fault_to_string(seL4_Word fsr)
{
    // https://wiki.osdev.org/Exceptions#Page_Fault
    switch (fsr) {
    case 0 | 4:
        return "read to a non-present page at ring 3";
    case 1 | 4:
        return "page-protection violation from read at ring 3";
    case 2 | 4:
        return "write to a non-present page at ring 3";
    case 3 | 4:
        return "page-protection violation from write at ring 3";
    case 16:
        // Note that seL4 currently does not implement the NX/XD bit
        // to mark a page as non-executable so we will never see the below message.
        return "instruction fetch from non-executable page";
    default:
        return "invalid FSR or unimplemented decoding";
    }
}

/* UBSAN decoding related functionality */
#define UBSAN_ARM64_BRK_IMM 0x5500
#define UBSAN_ARM64_BRK_MASK 0x00ff
#define ESR_COMMENT_MASK ((1 << 16) - 1)
#define ARM64_BRK_EC 60

/*
 * ABI defined by Clang's UBSAN enum SanitizerHandler:
 * https://github.com/llvm/llvm-project/blob/release/16.x/clang/lib/CodeGen/CodeGenFunction.h#L113
 */
enum UBSAN_CHECKS {
    UBSAN_ADD_OVERFLOW,
    UBSAN_BUILTIN_UNREACHABLE,
    UBSAN_CFI_CHECK_FAIL,
    UBSAN_DIVREM_OVERFLOW,
    UBSAN_DYNAMIC_TYPE_CACHE_MISS,
    UBSAN_FLOAT_CAST_OVERFLOW,
    UBSAN_FUNCTION_TYPE_MISMATCH,
    UBSAN_IMPLICIT_CONVERSION,
    UBSAN_INVALID_BUILTIN,
    UBSAN_INVALID_OBJC_CAST,
    UBSAN_LOAD_INVALID_VALUE,
    UBSAN_MISSING_RETURN,
    UBSAN_MUL_OVERFLOW,
    UBSAN_NEGATE_OVERFLOW,
    UBSAN_NULLABILITY_ARG,
    UBSAN_NULLABILITY_RETURN,
    UBSAN_NONNULL_ARG,
    UBSAN_NONNULL_RETURN,
    UBSAN_OUT_OF_BOUNDS,
    UBSAN_POINTER_OVERFLOW,
    UBSAN_SHIFT_OUT_OF_BOUNDS,
    UBSAN_SUB_OVERFLOW,
    UBSAN_TYPE_MISMATCH,
    UBSAN_ALIGNMENT_ASSUMPTION,
    UBSAN_VLA_BOUND_NOT_POSITIVE,
};

#ifdef CONFIG_ARM_HYPERVISOR_SUPPORT
static char *fault_usban_code_to_string(seL4_Word code)
{
    switch (code) {
    case UBSAN_ADD_OVERFLOW:
        return "add overflow";
    case UBSAN_BUILTIN_UNREACHABLE:
        return "builtin unreachable";
    case UBSAN_CFI_CHECK_FAIL:
        return "control-flow-integrity check fail";
    case UBSAN_DIVREM_OVERFLOW:
        return "division remainder overflow";
    case UBSAN_DYNAMIC_TYPE_CACHE_MISS:
        return "dynamic type cache miss";
    case UBSAN_FLOAT_CAST_OVERFLOW:
        return "float case overflow";
    case UBSAN_FUNCTION_TYPE_MISMATCH:
        return "function type mismatch";
    case UBSAN_IMPLICIT_CONVERSION:
        return "implicit conversion";
    case UBSAN_INVALID_BUILTIN:
        return "invalid builtin";
    case UBSAN_INVALID_OBJC_CAST:
        return "invalid objc cast";
    case UBSAN_LOAD_INVALID_VALUE:
        return "load invalid value";
    case UBSAN_MISSING_RETURN:
        return "missing return";
    case UBSAN_MUL_OVERFLOW:
        return "multiplication overflow";
    case UBSAN_NEGATE_OVERFLOW:
        return "negate overflow";
    case UBSAN_NULLABILITY_ARG:
        return "nullability argument";
    case UBSAN_NULLABILITY_RETURN:
        return "nullability return";
    case UBSAN_NONNULL_ARG:
        return "non-null argument";
    case UBSAN_NONNULL_RETURN:
        return "non-null return";
    case UBSAN_OUT_OF_BOUNDS:
        return "out of bounds access";
    case UBSAN_POINTER_OVERFLOW:
        return "pointer overflow";
    case UBSAN_SHIFT_OUT_OF_BOUNDS:
        return "shift out of bounds";
    case UBSAN_SUB_OVERFLOW:
        return "subtraction overflow";
    case UBSAN_TYPE_MISMATCH:
        return "type mismatch";
    case UBSAN_ALIGNMENT_ASSUMPTION:
        return "alignment assumption";
    case UBSAN_VLA_BOUND_NOT_POSITIVE:
        return "variable-length-array bound not positive";
    default:
        return "unknown reason";
    }
}
#endif

static void print_tcb_registers(seL4_UserContext *regs)
{
    FAULT(":=== FAULT BEGIN REGISTERS ===:\n");
    FAULT("pc   : %p | sp  : %p\n", (void *)regs->pc, (void *)regs->sp);
    FAULT("spsr : %p | x0  : %p\n", (void *)regs->spsr, (void *)regs->x0);
    FAULT("x1   : %p | x2  : %p\n", (void *)regs->x1, (void *)regs->x2);
    FAULT("x3   : %p | x4  : %p\n", (void *)regs->x3, (void *)regs->x4);
    FAULT("x5   : %p | x6  : %p\n", (void *)regs->x5, (void *)regs->x6);
    FAULT("x7   : %p | x8  : %p\n", (void *)regs->x7, (void *)regs->x8);
    FAULT("x16  : %p | x17 : %p\n", (void *)regs->x16, (void *)regs->x17);
    FAULT("x18  : %p | x29 : %p\n", (void *)regs->x18, (void *)regs->x29);
    FAULT("x30  : %p | x9  : %p\n", (void *)regs->x30, (void *)regs->x9);
    FAULT("x10  : %p | x11 : %p\n", (void *)regs->x10, (void *)regs->x11);
    FAULT("x12  : %p | x13 : %p\n", (void *)regs->x12, (void *)regs->x13);
    FAULT("x14  : %p | x15 : %p\n", (void *)regs->x14, (void *)regs->x15);
    FAULT("x19  : %p | x20 : %p\n", (void *)regs->x19, (void *)regs->x20);
    FAULT("x21  : %p | x22 : %p\n", (void *)regs->x21, (void *)regs->x22);
    FAULT("x23  : %p | x24 : %p\n", (void *)regs->x23, (void *)regs->x24);
    FAULT("x25  : %p | x26 : %p\n", (void *)regs->x25, (void *)regs->x26);
    FAULT("x27  : %p | x28 : %p\n", (void *)regs->x27, (void *)regs->x28);
    FAULT("tpidr_el0 : %p | tpidrro_el0 : %p\n", (void *)regs->tpidr_el0, (void *)regs->tpidrro_el0);
}

static void fault_aarch64_print_vm_fault()
{
    seL4_Word ip = seL4_GetMR(seL4_VMFault_IP);
    seL4_Word fault_addr = seL4_GetMR(seL4_VMFault_Addr);
    seL4_Word is_instruction = seL4_GetMR(seL4_VMFault_PrefetchFault);
    seL4_Word fsr = seL4_GetMR(seL4_VMFault_FSR);
    seL4_Word ec = fsr >> 26;
    seL4_Word il = fsr >> 25 & 1;
    seL4_Word iss = fsr & 0x1ffffffUL;
    FAULT(":=== VMFAULT BEGIN ERROR ===:\n");
    FAULT("ip   : %p | fault_addr: %p\n", (void *)ip, (void *)fault_addr);
    FAULT("fsr  : %p | %s\n", (void *)fsr, is_instruction ? "(instruction fault)" : "(data fault)");
    FAULT("ec   : %p | %s\n", (void *)ec, fault_ec_to_string(ec));
    FAULT("iss  : %p | il: %c\n", (void *)iss, il ? '1' : '0');

    if (ec == 0x24) {
        /* FIXME: Note, this is not a complete decoding of the fault! Just some of the more
           common fields!
        */
        seL4_Word dfsc = iss & 0x3f;
        bool ea = (iss >> 9) & 1;
        bool cm = (iss >> 8) & 1;
        bool s1ptw = (iss >> 7) & 1;
        bool wnr = (iss >> 6) & 1;
        FAULT("dfsc : %p | %s\n", (void *)dfsc, fault_data_abort_dfsc_to_string(dfsc));
        if (ea) {
            FAULT(" -- external abort\n");
        }
        if (cm) {
            FAULT(" -- cache maint\n");
        }
        if (s1ptw) {
            FAULT(" -- stage 2 fault for stage 1 page table walk\n");
        }
        if (wnr) {
            FAULT(" -- write not read\n");
        }
    }
}

static void fault_print(seL4_MessageInfo_t tag, seL4_Word badge)
{
    seL4_Word label;
    seL4_Error err;

    label = seL4_MessageInfo_get_label(tag);

    seL4_Word pd_id = badge & PD_MASK;
    seL4_Word tcb_cap = BASE_TCB_CAP + pd_id;

    FAULT(":====: Received fault :===:\n");
    FAULT("label: %p | badge: %p | tcb_cap: %p\n", (void *)label, (void *)badge, (void *)tcb_cap);

    seL4_UserContext regs;

    err = seL4_TCB_ReadRegisters(tcb_cap, false, 0, sizeof(seL4_UserContext) / sizeof(seL4_Word), &regs);
    if (err != seL4_NoError) {
        assert(!"error reading registers");
    }

    print_tcb_registers(&regs);

    switch (label) {
    case seL4_Fault_CapFault: {
        seL4_Word ip = seL4_GetMR(seL4_CapFault_IP);
        seL4_Word fault_addr = seL4_GetMR(seL4_CapFault_Addr);
        seL4_Word in_recv_phase = seL4_GetMR(seL4_CapFault_InRecvPhase);
        seL4_Word lookup_failure_type = seL4_GetMR(seL4_CapFault_LookupFailureType);
        seL4_Word bits_left = seL4_GetMR(seL4_CapFault_BitsLeft);
        seL4_Word depth_bits_found = seL4_GetMR(seL4_CapFault_DepthMismatch_BitsFound);
        seL4_Word guard_found = seL4_GetMR(seL4_CapFault_GuardMismatch_GuardFound);
        seL4_Word guard_bits_found = seL4_GetMR(seL4_CapFault_GuardMismatch_BitsFound);

        FAULT("TYPE: CAPFAULT\n");
        FAULT("ip: %p | fault_addr: %p\n", (void *)ip, (void *)fault_addr);
        FAULT("in_recv_phase: %s\n", in_recv_phase == 0 ? "false" : "true");
        FAULT("lookup_failure_type (%p):\n", (void *)lookup_failure_type);

        switch (lookup_failure_type) {
        case seL4_NoFailure:
            FAULT("    seL4_NoFailure\n");
            break;
        case seL4_InvalidRoot:
            FAULT("    seL4_InvalidRoot\n");
            break;
        case seL4_MissingCapability:
            FAULT("    seL4_MissingCapability\n");
            break;
        case seL4_DepthMismatch:
            FAULT("    seL4_DepthMismatch\n");
            break;
        case seL4_GuardMismatch:
            FAULT("    seL4_GuardMismatch\n");
            break;
        default:
            FAULT("    unknown failure type\n");
        }

        if (lookup_failure_type == seL4_MissingCapability || lookup_failure_type == seL4_DepthMismatch
            || lookup_failure_type == seL4_GuardMismatch) {
            FAULT("    bits_left: %p\n", (void *)bits_left);
        }
        if (lookup_failure_type == seL4_DepthMismatch) {
            FAULT("    depth_bits_found: %p\n", (void *)depth_bits_found);
        }
        if (lookup_failure_type == seL4_GuardMismatch) {
            FAULT("    guard_found: %p\n", (void *)guard_found);
            FAULT("    guard_bits_found: %p\n", (void *)guard_bits_found);
        }
        break;
    }
    case seL4_Fault_UserException: {
        FAULT("TYPE : USER_EXCEPTION\n");
        break;
    }
    case seL4_Fault_VMFault: {
        fault_aarch64_print_vm_fault();
    }
#ifdef CONFIG_ARM_HYPERVISOR_SUPPORT
    case seL4_Fault_VCPUFault: {
        seL4_Word esr = seL4_GetMR(seL4_VCPUFault_HSR);
        seL4_Word ec = esr >> 26;

        FAULT("VCPU FAULT (esr: %p):\n", (void *)esr);
        seL4_Word esr_comment = esr & ESR_COMMENT_MASK;
        if (ec == ARM64_BRK_EC && ((esr_comment & ~UBSAN_ARM64_BRK_MASK) == UBSAN_ARM64_BRK_IMM)) {
                /* We likely have a UBSAN check going off from a brk instruction */
            seL4_Word ubsan_code = esr_comment & UBSAN_ARM64_BRK_MASK;
            FAULT("  potential undefined behaviour detected by UBSAN for:\n");
            FAULT("    '%s'\n", fault_usban_code_to_string(ubsan_code));
        } else {
            FAULT("  unknown vCPU fault\n");
        }
        break;
    }
#endif
    default:
        FAULT("TYPE: UNKNOWN FAULT\n");
        break;
    }
}
