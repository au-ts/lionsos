#pragma once

#include <sel4/sel4.h>
#include <assert.h>

#define NO_ERR(X) assert(X == seL4_NoError)
#define TCB(x) BASE_TCB_CAP + x
#define SELF_TCB() microkit_cspace_root_slot_to_cptr(1)
