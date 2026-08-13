#pragma once
/*
 * Copyright 2021, Breakaway Consulting Pty. Ltd.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <microkit.h>
#include <sel4/sel4.h>
#include <stdbool.h>
#include <stdint.h>
#include <sddf/util/printf.h>
#include <inttypes.h>

#define LOG(...) sddf_printf("BACKTRACER | " __VA_ARGS__)

#define MAX_PDS 64

#define BASE_PD_TCB_CAP 10
#define BASE_SCHED_CONTEXT_CAP 138
#define BASE_NOTIFICATION_CAP 202

/* Sanity check that the architecture specific macro have been set. */
#if defined(__aarch64__)
#elif defined(__x86_64__)
#elif defined(__riscv64__)
#else
#error "Unknown or unsupported architecture for backtracing"
#endif

#ifdef __riscv64__
/*
 * Convert the fault status register given by the kernel into a string
 * describing what fault happened. The FSR is the 'scause' register.
 */
static char *riscv_fsr_to_string(seL4_Word fsr)
{
    switch (fsr) {
    case 0:
        return "Instruction address misaligned";
    case 1:
        return "Instruction access fault";
    case 2:
        return "Illegal instruction";
    case 3:
        return "Breakpoint";
    case 4:
        return "Load address misaligned";
    case 5:
        return "Load access fault";
    case 6:
        return "Store/AMO address misaligned";
    case 7:
        return "Store/AMO access fault";
    case 8:
        return "Environment call from U-mode";
    case 9:
        return "Environment call from S-mode";
    case 12:
        return "Instruction page fault";
    case 13:
        return "Load page fault";
    case 15:
        return "Store/AMO page fault";
    case 18:
        return "Software check";
    case 19:
        return "Hardware error";
    default:
        return "<Unexpected FSR>";
    }
}
#endif

#ifdef __aarch64__
static char *ec_to_string(uintptr_t ec)
{
    switch (ec) {
    case 0:
        return "Unknown reason";
    case 1:
        return "Trapped WFI or WFE instruction execution";
    case 3:
        return "Trapped MCR or MRC access with (coproc==0b1111) this is not "
               "reported using EC 0b000000";
    case 4:
        return "Trapped MCRR or MRRC access with (coproc==0b1111) this is not "
               "reported using EC 0b000000";
    case 5:
        return "Trapped MCR or MRC access with (coproc==0b1110)";
    case 6:
        return "Trapped LDC or STC access";
    case 7:
        return "Access to SVC, Advanced SIMD or floating-point functionality "
               "trapped";
    case 12:
        return "Trapped MRRC access with (coproc==0b1110)";
    case 13:
        return "Branch Target Exception";
    case 17:
        return "SVC instruction execution in AArch32 state";
    case 21:
        return "SVC instruction execution in AArch64 state";
    case 24:
        return "Trapped MSR, MRS or System instruction exuection in AArch64 state, "
               "this is not reported using EC 0xb000000, 0b000001 or 0b000111";
    case 25:
        return "Access to SVE functionality trapped";
    case 28:
        return "Exception from a Pointer Authentication instruction authentication "
               "failure";
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

static char *data_abort_dfsc_to_string(uintptr_t dfsc)
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
#endif

#ifdef __x86_64__
static char *page_fault_to_string(seL4_Word fsr)
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
#endif

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
static char *usban_code_to_string(seL4_Word code)
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
#if defined(__riscv64__)
    LOG("BACKTRACER | Registers: \n");
    LOG("BACKTRACER | pc : %#016lx\n", regs->pc);
    LOG("ra : %#016lx\n", regs->ra);
    LOG("s0 : %#016lx\n", regs->s0);
    LOG("s1 : %#016lx\n", regs->s1);
    LOG("s2 : %#016lx\n", regs->s2);
    LOG("s3 : %#016lx\n", regs->s3);
    LOG("s4 : %#016lx\n", regs->s4);
    LOG("s5 : %#016lx\n", regs->s5);
    LOG("s6 : %#016lx\n", regs->s6);
    LOG("s7 : %#016lx\n", regs->s7);
    LOG("s8 : %#016lx\n", regs->s8);
    LOG("s9 : %#016lx\n", regs->s9);
    LOG("s10 : %#016lx\n", regs->s10);
    LOG("s11 : %#016lx\n", regs->s11);
    LOG("a0 : %#016lx\n", regs->a0);
    LOG("a1 : %#016lx\n", regs->a1);
    LOG("a2 : %#016lx\n", regs->a2);
    LOG("a3 : %#016lx\n", regs->a3);
    LOG("a4 : %#016lx\n", regs->a4);
    LOG("a5 : %#016lx\n", regs->a5);
    LOG("a6 : %#016lx\n", regs->a6);
    LOG("t0 : %#016lx\n", regs->t0);
    LOG("t1 : %#016lx\n", regs->t1);
    LOG("t2 : %#016lx\n", regs->t2);
    LOG("t3 : %#016lx\n", regs->t3);
    LOG("t4 : %#016lx\n", regs->t4);
    LOG("t5 : %#016lx\n", regs->t5);
    LOG("t6 : %#016lx\n", regs->t6);
    LOG("tp : %#016lx\n", regs->tp);
#elif defined(__aarch64__)
    LOG("Registers: \n");
    LOG("pc : %#016lx\n", regs->pc);
    LOG("sp: %#016lx\n", regs->sp);
    LOG("spsr : %#016lx\n", regs->spsr);
    LOG("x0 : %#016lx\n", regs->x0);
    LOG("x1 : %#016lx\n", regs->x1);
    LOG("x2 : %#016lx\n", regs->x2);
    LOG("x3 : %#016lx\n", regs->x3);
    LOG("x4 : %#016lx\n", regs->x4);
    LOG("x5 : %#016lx\n", regs->x5);
    LOG("x6 : %#016lx\n", regs->x6);
    LOG("x7 : %#016lx\n", regs->x7);
    LOG("x8 : %#016lx\n", regs->x8);
    LOG("x16 : %#016lx\n", regs->x16);
    LOG("x17 : %#016lx\n", regs->x17);
    LOG("x18 : %#016lx\n", regs->x18);
    LOG("x29 : %#016lx\n", regs->x29);
    LOG("x30 : %#016lx\n", regs->x30);
    LOG("x9 : %#016lx\n", regs->x9);
    LOG("x10 : %#016lx\n", regs->x10);
    LOG("x11 : %#016lx\n", regs->x11);
    LOG("x12 : %#016lx\n", regs->x12);
    LOG("x13 : %#016lx\n", regs->x13);
    LOG("x14 : %#016lx\n", regs->x14);
    LOG("x15 : %#016lx\n", regs->x15);
    LOG("x19 : %#016lx\n", regs->x19);
    LOG("x20 : %#016lx\n", regs->x20);
    LOG("x21 : %#016lx\n", regs->x21);
    LOG("x22 : %#016lx\n", regs->x22);
    LOG("x23 : %#016lx\n", regs->x23);
    LOG("x24 : %#016lx\n", regs->x24);
    LOG("x25 : %#016lx\n", regs->x25);
    LOG("x26 : %#016lx\n", regs->x26);
    LOG("x27 : %#016lx\n", regs->x27);
    LOG("x28 : %#016lx\n", regs->x28);
    LOG("tpidr_el0 : %#016lx\n", regs->tpidr_el0);
    LOG("tpidrro_el0 : %#016lx\n", regs->tpidrro_el0);
#elif defined(__x86_64__)
    LOG("Registers: \n");
    LOG("rip : %#016lx\n", regs->rip);
    LOG("rsp : %#016lx\n", regs->rsp);
    LOG("rflags : %#016lx\n", regs->rflags);
    LOG("rax : %#016lx\n", regs->rax);
    LOG("rbx : %#016lx\n", regs->rbx);
    LOG("rcx : %#016lx\n", regs->rcx);
    LOG("rdx : %#016lx\n", regs->rdx);
    LOG("rsi : %#016lx\n", regs->rsi);
    LOG("rdi : %#016lx\n", regs->rdi);
    LOG("rbp : %#016lx\n", regs->rbp);
    LOG("r8 : %#016lx\n", regs->r8);
    LOG("r9 : %#016lx\n", regs->r9);
    LOG("r10 : %#016lx\n", regs->r10);
    LOG("r11 : %#016lx\n", regs->r11);
    LOG("r12 : %#016lx\n", regs->r12);
    LOG("r13 : %#016lx\n", regs->r13);
    LOG("r14 : %#016lx\n", regs->r14);
    LOG("r15 : %#016lx\n", regs->r15);
    LOG("fs_base : %#016lx\n", regs->fs_base);
    LOG("gs_base : %#016lx\n", regs->gs_base);
#endif
}

#ifdef __riscv64__
static void riscv_print_vm_fault()
{
    seL4_Word ip = seL4_GetMR(seL4_VMFault_IP);
    seL4_Word fault_addr = seL4_GetMR(seL4_VMFault_Addr);
    seL4_Word is_instruction = seL4_GetMR(seL4_VMFault_PrefetchFault);
    seL4_Word fsr = seL4_GetMR(seL4_VMFault_FSR);
    LOG("BACKTRACER | VMFault: ip=%#016lx\n", ip);
    puthex64(fault_addr);
    puts("  fsr=%#016lx\n", fsr);
    puts(is_instruction ? "(instruction fault)" : "(data fault)");
    puts("\n");
    puts("BACKTRACER | description of fault: ");
    puts(riscv_fsr_to_string(fsr));
    puts("\n");
}
#endif

#ifdef __x86_64__
static void x86_64_print_vm_fault()
{
    seL4_Word ip = seL4_GetMR(seL4_VMFault_IP);
    seL4_Word fault_addr = seL4_GetMR(seL4_VMFault_Addr);
    seL4_Word is_instruction = seL4_GetMR(seL4_VMFault_PrefetchFault);
    seL4_Word fsr = seL4_GetMR(seL4_VMFault_FSR);
    puts("BACKTRACER | VMFault: ip=");
    puthex64(ip);
    puts("  fault_addr=");
    puthex64(fault_addr);
    puts("  fsr=");
    puthex64(fsr);
    puts("  ");
    puts(is_instruction ? "(instruction fault)" : "(data fault)");
    puts("\n");

    puts("BACKTRACER | description of fault: ");
    puts(page_fault_to_string(fsr));
    puts("\n");
}
#endif

#ifdef __aarch64__
static void aarch64_print_vm_fault()
{
    seL4_Word ip = seL4_GetMR(seL4_VMFault_IP);
    seL4_Word fault_addr = seL4_GetMR(seL4_VMFault_Addr);
    seL4_Word is_instruction = seL4_GetMR(seL4_VMFault_PrefetchFault);
    seL4_Word fsr = seL4_GetMR(seL4_VMFault_FSR);
    seL4_Word ec = fsr >> 26;
    seL4_Word il = fsr >> 25 & 1;
    seL4_Word iss = fsr & 0x1ffffffUL;
    LOG("VMFault: ip=%#016lx  fault_addr=%#016lx\n", ip, fault_addr);
    LOG("    fsr=%#016lx  %s\n",  fsr, is_instruction ? "(instruction fault)" : "(data fault)");
    LOG("    ec: %#08lx  %s\n", ec, ec_to_string(ec));
    LOG("    il: %s    iss: %#08lx\n", il ? "1" : "0", iss);

    if (ec == 0x24) {
    /* FIXME: Note, this is not a complete decoding of the fault! Just some of
       the more common fields!
    */
        seL4_Word dfsc = iss & 0x3f;
        bool ea = (iss >> 9) & 1;
        bool cm = (iss >> 8) & 1;
        bool s1ptw = (iss >> 7) & 1;
        bool wnr = (iss >> 6) & 1;
        LOG("    dfsc = %s  (%#08lx)", data_abort_dfsc_to_string(dfsc), dfsc);
        if (ea) {
            sddf_printf(" -- external abort");
        }
        if (cm) {
            sddf_printf(" -- cache maint");
        }
        if (s1ptw) {
            sddf_printf(" -- stage 2 fault for stage 1 page table walk");
        }
        if (wnr) {
            sddf_printf(" -- write not read");
        }
        sddf_printf("\n");
    }
}
#endif

static void print_fault_error(microkit_child child, microkit_msginfo msginfo)
{
    seL4_Word tcb_cap = BASE_PD_TCB_CAP + child;
    seL4_Word label = microkit_msginfo_get_label(msginfo);
    seL4_Word err = { 0 };
    seL4_Word badge = child + 1;

    if (label == seL4_Fault_NullFault && child < MAX_PDS) {
    /* This is a request from our PD to become passive */
        err = seL4_SchedContext_Bind(BASE_SCHED_CONTEXT_CAP + child, BASE_NOTIFICATION_CAP + child);
        if (err != seL4_NoError) {
            LOG("could not bind scheduling context to notification "
                 "object\n");
        } else {
            LOG("PD id: '%d' is now passive!\n", child);
        }
        return;
    }

    LOG("received message %#08lx  badge: %#016lx  tcb cap: %#016lx\n", label, badge, tcb_cap);

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

        LOG("CapFault: ip=%#016lx  fault_addr=%#016lx  in_recv_phase=%s", ip, fault_addr, in_recv_phase == 0 ? "false" : "true");
        sddf_printf("  lookup_failure_type=");

        switch (lookup_failure_type) {
        case seL4_NoFailure:
            sddf_printf("seL4_NoFailure");
            break;
        case seL4_InvalidRoot:
            sddf_printf("seL4_InvalidRoot");
            break;
        case seL4_MissingCapability:
            sddf_printf("seL4_MissingCapability");
            break;
        case seL4_DepthMismatch:
            sddf_printf("seL4_DepthMismatch");
            break;
        case seL4_GuardMismatch:
            sddf_printf("seL4_GuardMismatch");
            break;
        default:
            sddf_printf("%#016lx", lookup_failure_type);
        }

        if (lookup_failure_type == seL4_MissingCapability || lookup_failure_type == seL4_DepthMismatch
            || lookup_failure_type == seL4_GuardMismatch) {
            sddf_printf("  bits_left=%#016lx", bits_left);
        }
        if (lookup_failure_type == seL4_DepthMismatch) {
            sddf_printf("  depth_bits_found=%#016lx", depth_bits_found);
        }
        if (lookup_failure_type == seL4_GuardMismatch) {
            sddf_printf("  guard_found=%#016lx", guard_found);
            sddf_printf("  guard_bits_found=%#016lx", guard_bits_found);
        }
        sddf_printf("\n");
        break;
    }
    case seL4_Fault_UserException: {
        LOG("UserException\n");
        break;
    }
    case seL4_Fault_VMFault: {
#if defined(__aarch64__)
        aarch64_print_vm_fault();
#elif defined(__riscv64__)
        riscv_print_vm_fault();
#elif defined(__x86_64__)
        x86_64_print_vm_fault();
#else
#error "Unknown architecture to print a VM fault for"
#endif
        break;
    }
#ifdef CONFIG_ARM_HYPERVISOR_SUPPORT
    case seL4_Fault_VCPUFault: {
        seL4_Word esr = seL4_GetMR(seL4_VCPUFault_HSR);
        seL4_Word ec = esr >> 26;

        LOG("received vCPU fault with ESR: %#016lx\n", esr);

        seL4_Word esr_comment = esr & ESR_COMMENT_MASK;
        if (ec == ARM64_BRK_EC && ((esr_comment & ~UBSAN_ARM64_BRK_MASK) == UBSAN_ARM64_BRK_IMM)) {
      /* We likely have a UBSAN check going off from a brk instruction */
            seL4_Word ubsan_code = esr_comment & UBSAN_ARM64_BRK_MASK;
            LOG("potential undefined behaviour detected by UBSAN for: "
                 "'%s'\n", usban_code_to_string(ubsan_code));
        } else {
            LOG("Unknown vCPU fault\n");
        }
        break;
    }
#endif
    default:
        LOG("Unknown fault: %#016lx\n", label);
        break;
    }
}
