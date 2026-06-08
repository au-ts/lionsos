#include <microkit.h>
#include <stdint.h>
#include <stddef.h>

void init() {
    microkit_dbg_puts("FAULTER | Faulter initialised!\n");
    // Cause a fault immediately
    volatile int* happy = (void*)UINTPTR_MAX;
    volatile int notHappy = *happy;
}
void notified(microkit_channel ch) {}
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo) {}
