#include <microkit.h>
#include <stdint.h>
#include <stddef.h>
#include <sddf/util/printf.h>
#define LOG(...) sddf_printf("FAULTER | " __VA_ARGS__)

void init() {
    LOG("Faulter initialised!\n");
    // Cause a fault immediately
    volatile int* happy = (void*)UINTPTR_MAX;
    volatile int notHappy = *happy;
    LOG("After dereference\n");
}
void notified(microkit_channel ch) {
    LOG("Notified!\n");
}
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo) {

    LOG("Protected!\n");
}
