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

#define BASE_PD_TCB_CAP 202
#define TEST_FUNC_ADDR 0x20001c

static void aarch64_print_vm_fault()
{
    seL4_Word ip = seL4_GetMR(seL4_VMFault_IP);
    seL4_Word fault_addr = seL4_GetMR(seL4_VMFault_Addr);
    seL4_Word is_instruction = seL4_GetMR(seL4_VMFault_PrefetchFault);
    seL4_Word fsr = seL4_GetMR(seL4_VMFault_FSR);
    sddf_printf("MON|ERROR: VMFault: ip=%0llx  fault_addr=%0llx  fsr=%0llx, %s\n", ip, fault_addr, fsr, is_instruction ? "(instruction fault)" : "(data fault)");
}


void init() {
    LOG("Backtracer initialised!\n");
    LOG("Printf test! %d\n", 100);
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

    if (label == seL4_Fault_VMFault)
    {
        aarch64_print_vm_fault();
    }
}

// modifies ctxt
void aarch64_callConvention_prologue(seL4_UserContext* ctxt, uintptr_t funcAddr)
{
    // Store x29, x30 to address sp-16, sp-8 respectively
    // stp x29, x30, [sp, #-16]!
    // mov x29, sp
    LOG("Pre jump PC: %p\n", (void*) ctxt->pc);
    // Set the link register to old PC
    ctxt->x30 = ctxt->pc;
    // Set the PC to the next function
    ctxt->pc = funcAddr;
    LOG("Post jump PC: %p\n", (void*) ctxt->pc);
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo,
                microkit_msginfo *reply_msginfo) {
	LOG("BEGIN Fault received!\n");
	uint64_t label = microkit_msginfo_get_label(msginfo);
	uint64_t count = microkit_msginfo_get_count(msginfo);
	printFaultType(label);
    seL4_UserContext ctxt = {0};
	LOG("read registers return: %d\n", seL4_TCB_ReadRegisters(BASE_PD_TCB_CAP + child, seL4_True, 0, sizeof(seL4_UserContext) / sizeof(seL4_Word), &ctxt)); 
	aarch64_callConvention_prologue(&ctxt, TEST_FUNC_ADDR);
	LOG("write registers return: %d\n", seL4_TCB_WriteRegisters(BASE_PD_TCB_CAP + child, seL4_True, 0, sizeof(seL4_UserContext) / sizeof(seL4_Word), &ctxt));
	LOG("END Fault received!\n");
    return seL4_True;
}

void notified(microkit_channel ch) {}
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo) {}
