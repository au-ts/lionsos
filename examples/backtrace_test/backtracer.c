#include <microkit.h>
#include <sddf/util/printf.h>
#define LOG(...) sddf_printf("BACKTRACER | " __VA_ARGS__)
#define FAULTS \
 X(seL4_Fault_CapFault) \
 X(seL4_Fault_VMFault) \
 X(seL4_Fault_UnknownSyscall) \
 X(seL4_Fault_UserException) \
 X(seL4_Fault_NullFault) \
 X(seL4_Fault_VPPIEvent) \
 X(seL4_Fault_VCPUFault)
 // X(seL4_Fault_TimeoutFault) 
 // X(seL4_Fault_VGICMaintenence)
 // X(seL4_Fault_DebugException)


void (*backtraceFunctions[])() = {NULL};

#define BASE_PD_TCB_CAP 202
#ifndef SHOW_BACKTRACE_FUNC_ADDR
#error "Please define SHOW_BACKTRACE_FUNC_ADDR to be the address of the show_backtrace function"
#endif

void init() {
    LOG("Initialised!");
}

void printFaultType(seL4_Word label)
{
    LOG("Fault type: ");
    switch (label)
    {
        #define X(value) case value: sddf_printf(#value "\n"); break;
        FAULTS
        #undef X
    }
}

#if defined(__aarch64__)
void callConvention_prologue(seL4_UserContext* ctxt, uintptr_t funcAddr)
{
    LOG("Pre jump PC: %p\n", (void*) ctxt->pc);
    // Set the link register to old PC
    ctxt->x30 = ctxt->pc;
    // Set the PC to the next function
    ctxt->pc = funcAddr;
    LOG("Post jump PC: %p\n", (void*) ctxt->pc);
}
#elif defined(__riscv)
#error "Unimplemented backtracer for riscv"
#elif defined(__x86_64__)
#error "Unimplemented backtracer for x86_64"
#else
#error "Unsupported architecture for backtracing"
#endif

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo,
                microkit_msginfo *reply_msginfo) {
	LOG("Fault received! Setting up backtrace...\n");
	uint64_t label = microkit_msginfo_get_label(msginfo);
	printFaultType(label);
    seL4_UserContext ctxt = {0};
    int readRegResult = seL4_TCB_ReadRegisters(BASE_PD_TCB_CAP + child, seL4_True, 0, sizeof(seL4_UserContext) / sizeof(seL4_Word), &ctxt);
    if (readRegResult != 0)
    {
        LOG("Failed to read registers for setting up backtrace jump! Got %d, expected %d\n", readRegResult, 0);
        return seL4_False;
    }
	callConvention_prologue(&ctxt, SHOW_BACKTRACE_FUNC_ADDR);
	int writeRegResult = seL4_TCB_WriteRegisters(BASE_PD_TCB_CAP + child, seL4_True, 0, sizeof(seL4_UserContext) / sizeof(seL4_Word), &ctxt);
    if (writeRegResult != 0)
    {
        LOG("Failed to write registers for setting up backtrace jump! Got %d, expected %d\n", writeRegResult, 0);
        return seL4_False;
    }
    return seL4_True;
}

void notified(microkit_channel ch) {}
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo) {}
