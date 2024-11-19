/*
 * Copyright 2022, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#include <microkit.h>
#include <sel4/benchmark_track_types.h>
#include <sel4/benchmark_utilisation_types.h>
#include <sddf/benchmark/bench.h>
#include <sddf/benchmark/sel4bench.h>
#include <sddf/serial/queue.h>
#include <sddf/util/fence.h>
#include <sddf/util/util.h>
#include <sddf/util/printf.h>
#include <serial_config.h>


#ifdef CONFIG_ARCH_ARM
#define LOG_BUFFER_CAP 7

/* Notification channels and TCB CAP offsets - ensure these align with .system file! */
#define START 1
#define STOP 2
#define INIT 3

#define PD_TOTAL            0
#define PD_ETH0_ID          1
#define PD_ETH1_ID          2
#define PD_VIRT_RX0_ID      3
#define PD_VIRT_RX1_ID      4
#define PD_VIRT_TX0_ID      5
#define PD_VIRT_TX1_ID      6
#define PD_FWD0_ID          7
#define PD_FWD1_ID          8
#define PD_SERIAL_VIRT_TX   10
#define PD_UART             9    

uintptr_t uart_base;
uintptr_t cyclecounters_vaddr;

ccnt_t counter_values[8];
counter_bitfield_t benchmark_bf;

#define SERIAL_TX_CH 0

char *serial_tx_data;
serial_queue_t *serial_tx_queue;
serial_queue_handle_t serial_tx_queue_handle;

#ifdef CONFIG_BENCHMARK_TRACK_KERNEL_ENTRIES
benchmark_track_kernel_entry_t *log_buffer;
#endif

struct bench *bench;

uint64_t total_cycles;
uint64_t idle_cycles;

char *counter_names[] = {
    "L1 i-cache misses",
    "L1 d-cache misses",
    "L1 i-tlb misses",
    "L1 d-tlb misses",
    "Instructions",
    "Branch mispredictions",
};

event_id_t benchmarking_events[] = {
    SEL4BENCH_EVENT_CACHE_L1I_MISS,
    SEL4BENCH_EVENT_CACHE_L1D_MISS,
    SEL4BENCH_EVENT_TLB_L1I_MISS,
    SEL4BENCH_EVENT_TLB_L1D_MISS,
    SEL4BENCH_EVENT_EXECUTE_INSTRUCTION,
    SEL4BENCH_EVENT_BRANCH_MISPREDICT,
};

static void print_pdid_name(uint64_t pd_id)
{
    /* PD names are hardcoded because there are two ethernet configs - what to do */
    switch (pd_id) {
    case PD_ETH0_ID:
        sddf_printf("eth_driver_0");
        break;
    case PD_ETH1_ID:
        sddf_printf("eth_driver_1");
        break;
    case PD_VIRT_RX0_ID:
        sddf_printf("eth0_virt_rx");
        break;
    case PD_VIRT_RX1_ID:
        sddf_printf("eth1_virt_rx");
        break;
    case PD_VIRT_TX0_ID:
        sddf_printf("eth0_virt_tx");
        break;
    case PD_VIRT_TX1_ID:
        sddf_printf("eth1_virt_tx");
        break;
    case PD_FWD0_ID:
        sddf_printf("eth0_forwarder");
        break;
    case PD_FWD1_ID:
        sddf_printf("eth1_forwarder");
        break;
    case PD_SERIAL_VIRT_TX:
        sddf_printf("serial_virt_tx");
        break;
    case PD_UART:
        sddf_printf("uart_driver");
        break;
    default:
        sddf_printf("unknown");
        break;
    }
}

#ifdef CONFIG_BENCHMARK_TRACK_UTILISATION
static void microkit_benchmark_start(void)
{
    seL4_BenchmarkResetThreadUtilisation(TCB_CAP);
    seL4_BenchmarkResetThreadUtilisation(BASE_TCB_CAP + PD_ETH0_ID);
    seL4_BenchmarkResetThreadUtilisation(BASE_TCB_CAP + PD_ETH1_ID);
    seL4_BenchmarkResetThreadUtilisation(BASE_TCB_CAP + PD_VIRT_RX0_ID);
    seL4_BenchmarkResetThreadUtilisation(BASE_TCB_CAP + PD_VIRT_RX1_ID);
    seL4_BenchmarkResetThreadUtilisation(BASE_TCB_CAP + PD_VIRT_TX0_ID);
    seL4_BenchmarkResetThreadUtilisation(BASE_TCB_CAP + PD_VIRT_TX1_ID);
    seL4_BenchmarkResetThreadUtilisation(BASE_TCB_CAP + PD_FWD0_ID);
    seL4_BenchmarkResetThreadUtilisation(BASE_TCB_CAP + PD_FWD1_ID);
    seL4_BenchmarkResetThreadUtilisation(BASE_TCB_CAP + PD_SERIAL_VIRT_TX);
    seL4_BenchmarkResetThreadUtilisation(BASE_TCB_CAP + PD_SERIAL_VIRT_TX);
    seL4_BenchmarkResetThreadUtilisation(BASE_TCB_CAP + PD_UART);
    seL4_BenchmarkResetLog();
}

static void microkit_benchmark_stop(uint64_t *total, uint64_t *number_schedules, uint64_t *kernel, uint64_t *entries)
{
    seL4_BenchmarkFinalizeLog();
    seL4_BenchmarkGetThreadUtilisation(TCB_CAP);
    uint64_t *buffer = (uint64_t *)&seL4_GetIPCBuffer()->msg[0];

    *total = buffer[BENCHMARK_TOTAL_UTILISATION];
    *number_schedules = buffer[BENCHMARK_TOTAL_NUMBER_SCHEDULES];
    *kernel = buffer[BENCHMARK_TOTAL_KERNEL_UTILISATION];
    *entries = buffer[BENCHMARK_TOTAL_NUMBER_KERNEL_ENTRIES];
}

static void microkit_benchmark_stop_tcb(uint64_t pd_id, uint64_t *total, uint64_t *number_schedules, uint64_t *kernel,
                                        uint64_t *entries)
{
    seL4_BenchmarkGetThreadUtilisation(BASE_TCB_CAP + pd_id);
    uint64_t *buffer = (uint64_t *)&seL4_GetIPCBuffer()->msg[0];

    *total = buffer[BENCHMARK_TCB_UTILISATION];
    *number_schedules = buffer[BENCHMARK_TCB_NUMBER_SCHEDULES];
    *kernel = buffer[BENCHMARK_TCB_KERNEL_UTILISATION];
    *entries = buffer[BENCHMARK_TCB_NUMBER_KERNEL_ENTRIES];
}

static void print_benchmark_details(uint64_t pd_id, uint64_t kernel_util, uint64_t kernel_entries,
                                    uint64_t number_schedules, uint64_t total_util)
{
    if (pd_id == PD_TOTAL) {
        sddf_printf("Total utilisation details: \n");
    } else {
        sddf_printf("Utilisation details for PD: ");
        print_pdid_name(pd_id);
        sddf_printf(" (%lx)\n", pd_id);
    }
    sddf_printf("{\nKernelUtilisation:  %lx\nKernelEntries:  %lx\nNumberSchedules:  %lx\nTotalUtilisation:  %lx\n}\n",
                kernel_util, kernel_entries, number_schedules, total_util);
}
#endif

#ifdef CONFIG_BENCHMARK_TRACK_KERNEL_ENTRIES
static inline void seL4_BenchmarkTrackDumpSummary(benchmark_track_kernel_entry_t *logBuffer, uint64_t logSize)
{
    seL4_Word index = 0;
    seL4_Word syscall_entries = 0;
    seL4_Word fastpaths = 0;
    seL4_Word interrupt_entries = 0;
    seL4_Word userlevelfault_entries = 0;
    seL4_Word vmfault_entries = 0;
    seL4_Word debug_fault = 0;
    seL4_Word other = 0;

    while (logBuffer[index].start_time != 0 && index < logSize) {
        if (logBuffer[index].entry.path == Entry_Syscall) {
            if (logBuffer[index].entry.is_fastpath) {
                fastpaths++;
            }
            syscall_entries++;
        } else if (logBuffer[index].entry.path == Entry_Interrupt) {
            interrupt_entries++;
        } else if (logBuffer[index].entry.path == Entry_UserLevelFault) {
            userlevelfault_entries++;
        } else if (logBuffer[index].entry.path == Entry_VMFault) {
            vmfault_entries++;
        } else if (logBuffer[index].entry.path == Entry_DebugFault) {
            debug_fault++;
        } else {
            other++;
        }

        index++;
    }

    sddf_printf("Number of system call invocations  %llx and fastpaths  %llx\n", syscall_entries, fastpaths);
    sddf_printf("Number of interrupt invocations  %llx\n", interrupt_entries);
    sddf_printf("Number of user-level faults  %llx\n", userlevelfault_entries);
    sddf_printf("Number of VM faults  %llx\n", vmfault_entries);
    sddf_printf("Number of debug faults  %llx\n", debug_fault);
    sddf_printf("Number of others  %llx\n", other);
}
#endif


void notified(microkit_channel ch)
{
    switch (ch) {
    case START:
#ifdef MICROKIT_CONFIG_benchmark
        total_cycles = __atomic_load_n(&bench->ts, __ATOMIC_RELAXED);
        idle_cycles = __atomic_load_n(&bench->ccount, __ATOMIC_RELAXED);

        sel4bench_reset_counters();
        THREAD_MEMORY_RELEASE();
        sel4bench_start_counters(benchmark_bf);

#ifdef CONFIG_BENCHMARK_TRACK_UTILISATION
        microkit_benchmark_start();
#endif

#ifdef CONFIG_BENCHMARK_TRACK_KERNEL_ENTRIES
        seL4_BenchmarkResetLog();
#endif
#endif

        break;
    case STOP:
#ifdef MICROKIT_CONFIG_benchmark
        total_cycles = __atomic_load_n(&bench->ts, __ATOMIC_RELAXED) - total_cycles;
        idle_cycles = __atomic_load_n(&bench->ccount, __ATOMIC_RELAXED) - idle_cycles;

        sddf_printf("Total cycles: %lx\n", total_cycles);
        sddf_printf("Idle cycles: %lx\n", idle_cycles);

        sel4bench_get_counters(benchmark_bf, &counter_values[0]);
        sel4bench_stop_counters(benchmark_bf);

        sddf_printf("{\n");
        for (int i = 0; i < ARRAY_SIZE(benchmarking_events); i++) {
            sddf_printf("%s: %lX\n", counter_names[i], counter_values[i]);
        }
        sddf_printf("}\n");
#endif

#ifdef CONFIG_BENCHMARK_TRACK_UTILISATION
        uint64_t total;
        uint64_t kernel;
        uint64_t entries;
        uint64_t number_schedules;
        microkit_benchmark_stop(&total, &number_schedules, &kernel, &entries);
        print_benchmark_details(PD_TOTAL, kernel, entries, number_schedules, total);

        microkit_benchmark_stop_tcb(PD_ETH0_ID, &total, &number_schedules, &kernel, &entries);
        print_benchmark_details(PD_ETH0_ID, kernel, entries, number_schedules, total);

        microkit_benchmark_stop_tcb(PD_ETH1_ID, &total, &number_schedules, &kernel, &entries);
        print_benchmark_details(PD_ETH1_ID, kernel, entries, number_schedules, total);

        microkit_benchmark_stop_tcb(PD_VIRT_RX0_ID, &total, &number_schedules, &kernel, &entries);
        print_benchmark_details(PD_VIRT_RX0_ID, kernel, entries, number_schedules, total);

        microkit_benchmark_stop_tcb(PD_VIRT_RX1_ID, &total, &number_schedules, &kernel, &entries);
        print_benchmark_details(PD_VIRT_RX1_ID, kernel, entries, number_schedules, total);
        
        microkit_benchmark_stop_tcb(PD_VIRT_TX0_ID, &total, &number_schedules, &kernel, &entries);
        print_benchmark_details(PD_VIRT_TX0_ID, kernel, entries, number_schedules, total);

        microkit_benchmark_stop_tcb(PD_VIRT_TX1_ID, &total, &number_schedules, &kernel, &entries);
        print_benchmark_details(PD_VIRT_TX1_ID, kernel, entries, number_schedules, total);

        microkit_benchmark_stop_tcb(PD_FWD0_ID, &total, &number_schedules, &kernel, &entries);
        print_benchmark_details(PD_FWD0_ID, kernel, entries, number_schedules, total);

        microkit_benchmark_stop_tcb(PD_FWD1_ID, &total, &number_schedules, &kernel, &entries);
        print_benchmark_details(PD_FWD1_ID, kernel, entries, number_schedules, total);

        microkit_benchmark_stop_tcb(PD_SERIAL_VIRT_TX, &total, &number_schedules, &kernel, &entries);
        print_benchmark_details(PD_SERIAL_VIRT_TX, kernel, entries, number_schedules, total);

        microkit_benchmark_stop_tcb(PD_UART, &total, &number_schedules, &kernel, &entries);
        print_benchmark_details(PD_UART, kernel, entries, number_schedules, total);
#endif

#ifdef CONFIG_BENCHMARK_TRACK_KERNEL_ENTRIES
        entries = seL4_BenchmarkFinalizeLog();
        sddf_printf("KernelEntries:  %llx\n", entries);
        seL4_BenchmarkTrackDumpSummary(log_buffer, entries);
#endif

        sddf_printf("\n\n\n\n");

        break;
    case SERIAL_TX_CH:
        // Nothing to do
        break;
    default:
        sddf_printf("Bench thread notified on unexpected channel\n");
    }
}

void init(void)
{
    serial_cli_queue_init_sys(microkit_name, NULL, NULL, NULL, &serial_tx_queue_handle, serial_tx_queue, serial_tx_data);
    serial_putchar_init(SERIAL_TX_CH, &serial_tx_queue_handle);
#ifdef MICROKIT_CONFIG_benchmark
    bench = (void *)cyclecounters_vaddr;
    sel4bench_init();
    seL4_Word n_counters = sel4bench_get_num_counters();

    counter_bitfield_t mask = 0;

    for (seL4_Word counter = 0; counter < n_counters; counter++) {
        if (counter >= ARRAY_SIZE(benchmarking_events)) {
            break;
        }
        sel4bench_set_count_event(counter, benchmarking_events[counter]);
        mask |= BIT(counter);
    }

    sel4bench_reset_counters();
    sel4bench_start_counters(mask);
    benchmark_bf = mask;
#else
    sddf_dprintf("BENCH|LOG: Bench running in debug mode, no access to counters\n");
#endif

    /* Notify the idle thread that the sel4bench library is initialised. */
    microkit_notify(INIT);

#ifdef CONFIG_BENCHMARK_TRACK_KERNEL_ENTRIES
    int res_buf = seL4_BenchmarkSetLogBuffer(LOG_BUFFER_CAP);
    if (res_buf) {
        sddf_printf("Could not set log buffer:  %llx\n", res_buf);
    } else {
        sddf_printf("Log buffer set\n");
    }
#endif
}

seL4_Bool fault(microkit_child id, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    sddf_printf("BENCH|LOG: Faulting PD ");
    print_pdid_name(id);
    sddf_printf(" (%x)\n", id);

    seL4_UserContext regs;
    seL4_TCB_ReadRegisters(BASE_TCB_CAP + id, false, 0, sizeof(seL4_UserContext) / sizeof(seL4_Word), &regs);
    sddf_printf("Registers: \npc : %lx\nspsr : %lx\nx0 : %lx\nx1 : %lx\nx2 : %lx\nx3 : %lx\nx4 : %lx\nx5 : %lx\nx6 : %lx\nx7 : %lx\n",
                regs.pc, regs.spsr, regs.x0, regs.x1, regs.x2, regs.x3, regs.x4, regs.x5, regs.x6, regs.x7);

    switch (microkit_msginfo_get_label(msginfo)) {
    case seL4_Fault_CapFault: {
        uint64_t ip = seL4_GetMR(seL4_CapFault_IP);
        uint64_t fault_addr = seL4_GetMR(seL4_CapFault_Addr);
        uint64_t in_recv_phase = seL4_GetMR(seL4_CapFault_InRecvPhase);
        sddf_printf("CapFault: ip=%lx  fault_addr=%lx  in_recv_phase=%s\n", ip, fault_addr,
                    (in_recv_phase == 0 ? "false" : "true"));
        break;
    }
    case seL4_Fault_UserException: {
        sddf_printf("UserException\n");
        break;
    }
    case seL4_Fault_VMFault: {
        uint64_t ip = seL4_GetMR(seL4_VMFault_IP);
        uint64_t fault_addr = seL4_GetMR(seL4_VMFault_Addr);
        uint64_t is_instruction = seL4_GetMR(seL4_VMFault_PrefetchFault);
        uint64_t fsr = seL4_GetMR(seL4_VMFault_FSR);
        sddf_printf("VMFault: ip=%lx  fault_addr=%lx  fsr=%lx %s\n", ip, fault_addr, fsr,
                    (is_instruction ? "(instruction fault)" : "(data fault)"));
        break;
    }
    default:
        sddf_printf("Unknown fault\n");
        break;
    }

    return seL4_False;
}
#endif

#ifdef CONFIG_ARCH_RISCV

void init(void) {}
void notified(microkit_channel ch) {}

#endif
