#pragma once

#include "sel4/functions.h"
#include "sel4/shared_types_gen.h"
#include "types.h"
#include <sddf/util/printf.h>
#include "../ipc.h"
#define REC(...) do {sddf_printf("RECORD | "); sddf_printf(__VA_ARGS__);} while (0)

static inline void rr_init_record()
{
    REC("init\n");
}

// We store ipc messages
static inline void rr_record_store_ipc_msg(seL4_Word cycle_count, seL4_Word source_child, seL4_Word badge,
                                           seL4_MessageInfo_t msg)
{
    switch (rr_ipc_get_type(msg, badge)) {
    case rr_IPCType_BlockChecker: {
        assert(false);
    } break;
    case rr_IPCType_SenderReply: {
        // Format: [cycle_count] Reply [source_child] [target_child] [badge] [message]
        seL4_Word target_child = rr_channel_to_target_child_id[rrer_source_ch_to_target_ch(rr_recv_source_channel)];
        REC("0x%lx Reply child_%lu child_%lu %lx %lx", cycle_count, source_child, target_child, badge, msg.words[0]);
        seL4_Word msglen = seL4_MessageInfo_get_length(msg);
        for (seL4_Word i = 0; i < msglen; i++) {
            sddf_printf(" %lx", seL4_GetMR(i));
        }
        sddf_printf("\n");
    } break;
    case rr_IPCType_Msg: {
        assert(false);
    } break;
    case rr_IPCType_Fault: {
        assert(!"TODO: handle faults");
    } break;
    case rr_IPCType_Call: {
        // Format: [cycle_count] msg [source_child] [source_channel] [target_child] [target_channel] [badge] [message]
        // needs to be channel mask, the other is for ntfns.
        seL4_Word source_ch = badge & CHANNEL_MASK;
        // not sus
        seL4_Word target_child = rr_channel_to_target_child_id[source_ch];
        seL4_Word target_ch = rrer_source_ch_to_target_ch(source_ch);
        REC("0x%lx %s child_%lu channel_%lu child_%lu channel_%lu %lx %lx", cycle_count,
            rr_ipc_type_to_string(rr_IPCType_Call), source_child, source_ch, target_child, target_ch, badge,
            msg.words[0]);
        seL4_Word msglen = seL4_MessageInfo_get_length(msg);
        for (seL4_Word i = 0; i < msglen; i++) {
            sddf_printf(" %lx", seL4_GetMR(i));
        }
        sddf_printf("\n");
    } break;
    case rr_IPCType_Ntfn: {
        // only allowed for ntfns.
        seL4_Word source_ch = rr_badge_to_channel_id(badge);
        // not sus
        seL4_Word target_child = rr_channel_to_target_child_id[source_ch];
        seL4_Word target_ch = rrer_source_ch_to_target_ch(source_ch);
        REC("0x%lx %s child_%lu channel_%lu child_%lu channel_%lu %lx %lx\n", cycle_count,
            rr_ipc_type_to_string(rr_IPCType_Ntfn), source_child, source_ch, target_child, target_ch, badge,
            msg.words[0]);
    } break;
    }
}
// We store scheduler events
static inline void rr_record_store_scheduler_event(seL4_Word cycle_count, seL4_Word child_id, rr_ChildState_e new_state)
{
    REC("0x%lx scheduler child_%lu %s\n", cycle_count, child_id, rr_child_state_to_string(new_state));
}
