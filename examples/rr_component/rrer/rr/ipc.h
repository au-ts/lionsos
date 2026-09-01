#pragma once
#include "base.h"
#include "scheduler.h"
static inline void rrer_handle_ipc();
static inline microkit_channel rrer_ch_to_target_(microkit_channel ch);

static inline microkit_channel rrer_ch_to_target_(microkit_channel ch) {
    if (ch % 2 == 0) return ch + 1;
    return ch - 1;
}

static inline void rrer_handle_ipc() {
    // methodology:
    // 1. Blocking send
    //    - Emulate blocking by only handling IPC when we realised that the current scheduled thread is blocked.
    //      And then setting it's priority to be it's original.
    //    - Emulate sending by having a dedicated sender? We need something that will block on the kernel.
    //      This ensures that it can be received by a NB recv.
    // 2. Blocking recv
    //    - Emulate blocking works by default. We notice that the PD is blocked, and set it as blocked and
    //      settign the priority to be it's original.
    //    - Emulating recv is fine, when we handle IPC we just have to reschedule the sender and the receiver.
    // 3. NB send
    //    - Emulate nonblocking. No comment.
    //    - Emulate send - We MUST receive the message so that we can check if it can be actually sent.
    //      Not sure how to do this. In order to actually receive the message, we must be determined
    //      as "blocked by recv" by the kernel. And then we just need to perform an NBSend.
    // 3. NB recv
    //    - Emulate nonblocking. No comment.
    //    - Emulate recv - We must be able to have a TCB that is determined as blocked by send, whenever
    //      a blocking send is.
    seL4_Word badge;
    LOG("BEGIN HANDLE IPC\n");
    for (seL4_MessageInfo_t result = seL4_NBRecv(INPUT_CAP, &badge, REPLY_CAP); badge != 0; result = seL4_NBRecv(INPUT_CAP, &badge, REPLY_CAP)) {
        LOG("Received from 0x%lx\n", badge);
        // the badge for microkit is the index of the 1 bit, which indicates the index.
        seL4_Word idx = 0;
        do {
            if (badge & 1)
            {
                // find target
                seL4_Word ch = rrer_ch_to_target_(idx);
                LOG("Forwarding to 0x%lx, %lu\n", ch, idx);
                seL4_NBSend(ch + BASE_OUTPUT_NOTIFICATION_CAP, result);

                // we must do a reschedule now.
                // and also unblock the target if it was blocked.
                if (rr_children_arr[ch].sched_state == rr_ChildState_Blocked) {
                    rr_children_arr[ch].sched_state = rr_ChildState_Schedulable;
                }
                rrer_reschedule();
            }
            idx++;
            badge >>= 1;
        } while (badge != 0);
    }
    LOG("END HANDLE IPC\n");
}
