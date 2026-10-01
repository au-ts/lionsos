#pragma once

// Defines the API which is used for interacting with a storage backend, both for
// reading and writing. This is a combination of a very very basic block driver abstraction
// and the recorder "file" structure.
// The structure is the following:
//   0x000000 .. 0x?????? | Metadata (holds offsets into instructions, num instructions, data, numdata, num children, etc)
//   0x?????? .. middle   | Instructions (stack growing to higher offsets)
//   middle   .. MAX_SIZE | Data         (stack growing to lower offsets)
// Hopefully this should ensure that we have enough size.

// Instructions are fixed size, and have the following structure:
//   [cycle_count] [instruction_type] [instruction_args]
// (For now we will not pack these, if performance is bad we can improve it later.)
// Instruction args depend on the instruction type.
// Valid instruction types and their arguments:
//   scheduler [target_child] [new_state]
//   notification [source_ch] [badge] [tag]
//   call [source_ch] [badge] [tag] [data_offset]
//   reply [source_ch] [badge] [tag] [data_offset]
//   (and maybe more?)
// We can determine the number of data_words in a call/reply via the tag
// We can determine the target_ch, target_child and source_child from the source_ch.
// (hopefully should be deterministically determined).

// The data is written to the datastack in the correct order.
// Similarly for instructions
// Every time we write an event occurs we do the following:
// 1. Write the instruction
// 2. Write the data
// 3. Increment the sizeof data and instruction count in the metadata section.
// 4. Continue.
// During initialisation for recording, we must do the following:
// 1. Write magic and metadata for children.
// 2. Initialise a relevant handle for keeping track of both stacks and their respective metadata.

// During initialisation for replaying, we must do the following:
// 1. Read magic and children metadata, check for errors.
// 2. Initialise a relevant handle for iterating through the instruction list and reading metadata.
// During replay we'll have a different main program which will control the scheduler and other relevant
// pd manipulators.
// (BUG: For some reason the PMU cycle counter increments by like 2 whenever we unsuspend a child)

// *Do not include this file directly.*
#include "block.h"
#include "sel4/functions.h"
#include "sel4/shared_types_gen.h"
#include <sddf/util/printf.h>
#include "../base.h"
#include "../ipc.h"

#define RR_STORAGE_HANDLE_STORAGE_SIZE 1024
#define RR_STORAGE_MAGIC "potator"
#define RR_MAX_CHILDREN 32
#define STR_NO_ERR(x) assert(x)

// We treat the storage backend similarly to a elf file.
// It two main sections - one for storing variable-sized data (like .data),
// and the other for the instructions.
// A handle to the storage unit.
typedef struct __attribute__((packed)) rr_storage_metadata {
    uint8_t magic[sizeof(RR_STORAGE_MAGIC)];
    // The text section holds data in the given shape
    // [cycle count] [unit type] [specific unit args]
    // Each part is word size, with total size being sizeof(rr_storage_unit_t)
    // References to any stored data will be relaive to data_offset
    seL4_Word children_num; // for error checking
    seL4_Word children_data_begin;
    seL4_Word children_data_end;
    seL4_Word inst_num; // for error checking
    seL4_Word inst_begin;
    seL4_Word inst_end;
    seL4_Word data_begin; // The beginning will be growing upwards (becoming smaller)
    seL4_Word data_end; // marks the end of the storage device.
} rr_storage_metadata_t;

typedef enum rr_storage_unit_type {
    rr_storage_unit_REPLY = 1,
    rr_storage_unit_MSG, // no msgs
    rr_storage_unit_CALL,
    rr_storage_unit_NTFN,
    rr_storage_unit_SCHED,
    _rr_storage_unit_max = 1 << 63,
} rr_storage_unit_type_e;

typedef struct rr_storage_msg_args {
    seL4_Word source_chan; // We can determine the source and target children from here.
    seL4_Word target_chan;
    seL4_MessageInfo_t msginfo;
    seL4_Word badge;
    seL4_Word data_offset;
} rr_storage_msg_args_t;

typedef struct rr_storage_call_args {
    seL4_Word source_chan;
    seL4_Word target_chan;
    seL4_MessageInfo_t msginfo;
    seL4_Word badge;
    seL4_Word data_offset;
} rr_storage_call_args_t;

typedef struct rr_storage_ntfn_args {
    seL4_Word source_chan;
    seL4_Word target_chan;
    seL4_MessageInfo_t msginfo;
    seL4_Word badge;
} rr_storage_ntfn_args_t;

typedef struct rr_storage_reply_args {
    seL4_Word source_child;
    seL4_Word target_child;
    seL4_MessageInfo_t msginfo;
    seL4_Word badge;
    seL4_Word data_offset;
} rr_storage_reply_args_t;

// The backing type for scheduling events
typedef struct rr_storage_sched_args {
    seL4_Word child;
    rr_ChildState_e state; // also word-sized.
} rr_storage_sched_args_t;

typedef struct rr_storage_unit {
    seL4_Word cycle_count;
    rr_storage_unit_type_e unit_type;
    union {
        rr_storage_msg_args_t msg_args;
        rr_storage_sched_args_t sched_args;
        rr_storage_reply_args_t reply_args;
        rr_storage_call_args_t call_args;
        rr_storage_ntfn_args_t ntfn_args;
        seL4_Word _max_size[6];
    } args;
} rr_storage_unit_t;
static inline void rr_storage_unit_print(const rr_storage_unit_t *unit);

// Use the handle paradigm here so that this can be more easily reused later for replaying?
typedef struct rr_storage_handle {
    rr_storage_metadata_t metadata;
    seL4_Word iptr; // instruction pointer, in bytes.
} rr_storage_handle_t;

// satisfied by included backend. (which this file is included by).
static inline void rr_init_storage_backend();

// synchronous read and writes for now.
// This is all the backend needs to supply for now.
static inline bool rr_storage_write(uint64_t byte_offset, const uint8_t *bytes, uint64_t num_bytes);
static inline bool rr_storage_read(uint64_t byte_offset, uint8_t *result, uint64_t num_bytes);
static inline seL4_Word rr_storage_get_size();

// this is for recording, it will give us a relevant handle, and write all the necessary preliminary data.
static inline rr_storage_handle_t rr_storage_start()
{
    rr_init_storage_backend();
    // print the first 8 chars of the storage.
    uint8_t chars[8] = { 0 };
    STR_NO_ERR(rr_storage_read(0, chars, 8));
    for (int i = 0; i < 8; i++) {
        sddf_dprintf("%c ", chars[i]);
    }
    sddf_dprintf("\n");
    seL4_Word size = rr_storage_get_size();
    seL4_Word child_begin = sizeof(rr_storage_metadata_t);
    seL4_Word child_end = child_begin + sizeof(rr_Child_t) * rr_children_num;
    rr_storage_handle_t handle = {
        .metadata = {
            .children_num = rr_children_num,
            .children_data_begin = child_begin,
            .children_data_end = child_end,
            .inst_num = 0,
            .inst_begin = child_end,
            .inst_end = child_end,
            .data_begin = size,
            .data_end = size,
        },
        // not relevant for the recorder.
        .iptr = 0,
    };
    memcpy(handle.metadata.magic, RR_STORAGE_MAGIC, sizeof(RR_STORAGE_MAGIC));
    // Write the metadata in now.
    STR_NO_ERR(rr_storage_write(0, (const uint8_t *)&(handle.metadata), sizeof(rr_storage_metadata_t)));
    // Write in the children data.
    for (int i = 0; i < rr_children_num; i++) {
        STR_NO_ERR(rr_storage_write(handle.metadata.children_data_begin + i * sizeof(rr_Child_t),
                                    (const uint8_t *)&rr_children_arr[i], sizeof(rr_Child_t)));
    }
    return handle;
}

static inline void rr_storage_update_metadata(const rr_storage_handle_t *handle)
{
    STR_NO_ERR(rr_storage_write(0, (const uint8_t *)&(handle->metadata), sizeof(rr_storage_metadata_t)));
}

// This is for the replayer.
// This is to be called after the main initialisation.
static inline rr_storage_handle_t rr_storage_retrieve()
{
    rr_init_storage_backend();
    rr_storage_metadata_t metadata = { 0 };

    // just realised rr_storage_read will abort if it fails, so it physically won't hit this assertion.
    STR_NO_ERR(rr_storage_read(0, (uint8_t *)&metadata, sizeof(metadata)));
    STORAGE_LOG("Metadata:\n");
    STORAGE_LOG("    magic[sizeof(RR_STORAGE_MAGIC)] = %s\n", metadata.magic);
    STORAGE_LOG("    children_num                    = %lu\n", metadata.children_num);
    STORAGE_LOG("    children_data_begin             = %lx\n", metadata.children_data_begin);
    STORAGE_LOG("    children_data_end               = %lx\n", metadata.children_data_end);
    STORAGE_LOG("    inst_num                        = %lu\n", metadata.inst_num);
    STORAGE_LOG("    inst_begin                      = %lx\n", metadata.inst_begin);
    STORAGE_LOG("    inst_end                        = %lx\n", metadata.inst_end);
    STORAGE_LOG("    data_begin                      = %lx\n", metadata.data_begin);
    STORAGE_LOG("    data_end                        = %lx\n", metadata.data_end);

    assert(memcmp(metadata.magic, RR_STORAGE_MAGIC, sizeof(RR_STORAGE_MAGIC)) == 0);
    assert(metadata.children_data_begin <= metadata.children_data_end);
    assert(metadata.children_num == rr_children_num);
    assert(metadata.data_begin <= metadata.data_end);
    assert(metadata.inst_begin <= metadata.inst_end);
    assert(metadata.inst_num * sizeof(rr_storage_unit_t) == metadata.inst_end - metadata.inst_begin);
    assert(metadata.children_num * sizeof(rr_Child_t) == metadata.children_data_end - metadata.children_data_begin);

    for (int i = 0; i < rr_children_num; i++) {
        rr_Child_t child = {0};
        rr_storage_read(metadata.children_data_begin + i * sizeof(child), (uint8_t*)&child, sizeof(child));
        STORAGE_LOG("    child[%d].id                   = %lx\n", i, child.id);
        STORAGE_LOG("    child[%d].prio                 = %lx\n", i, child.priority);
    }

    return (rr_storage_handle_t) { .metadata = metadata, .iptr = metadata.inst_begin };
}

// stores what is currently in the IPC buffer into the data.
// I'm pretty sure that the IPC buffer would not have been clobbered yet.
// The data counter only increments when everything has been written.
// Returns a "offset" into the respective data region.
static inline seL4_Word rr_storage_store_ipc_data(rr_storage_handle_t *handle, seL4_MessageInfo_t msg)
{
    seL4_Word num_words = seL4_MessageInfo_get_length(msg);
    // grows upwards/to lesser values.
    seL4_Word begin = handle->metadata.data_begin;
    for (int i = 0; i < num_words; i++) {
        seL4_Word reg = seL4_GetMR(i);
        rr_storage_write(begin - i * sizeof(seL4_Word), (const uint8_t *)&reg, sizeof(seL4_Word));
    }
    begin -= num_words * sizeof(seL4_Word);
    handle->metadata.data_begin = begin;

    // definitely slow, will optimize later.
    rr_storage_update_metadata(handle);
    return begin;
}

// We store ipc messages
// The instruction counter only increments once both the data and instruction has been written.
static inline void rr_storage_store_ipc_msg(rr_storage_handle_t *handle, seL4_Word cycle_count, seL4_Word source_child,
                                            seL4_Word badge, seL4_MessageInfo_t msg)
{
    rr_storage_unit_t unit = {
        .cycle_count = cycle_count,
    };
    switch (rr_ipc_get_type(msg, badge)) {
    case rr_IPCType_BlockChecker: {
        assert(false);
    } break;
    case rr_IPCType_SenderReply: {
        // Format: [cycle_count] Reply [source_child] [target_child] [badge] [message]
        seL4_Word target_child = rr_channel_to_target_child_id[rrer_source_ch_to_target_ch(rr_recv_source_channel)];

        unit.unit_type = rr_storage_unit_REPLY;
        unit.args.reply_args = (rr_storage_reply_args_t) {
            .badge = badge,
            .msginfo = msg,
            .data_offset = rr_storage_store_ipc_data(handle, msg),
            .source_child = source_child,
            .target_child = target_child,
        };
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
        seL4_Word source_chan = badge & CHANNEL_MASK;
        // not sus
        seL4_Word target_chan = rrer_source_ch_to_target_ch(source_chan);

        unit.unit_type = rr_storage_unit_CALL;
        unit.args.call_args = (rr_storage_call_args_t) {
            .badge = badge,
            .msginfo = msg,
            .data_offset = rr_storage_store_ipc_data(handle, msg),
            .source_chan = source_chan,
            .target_chan = target_chan, // uwu
        };
    } break;
    case rr_IPCType_Ntfn: {
        // only allowed for ntfns.
        seL4_Word source_chan = rr_badge_to_channel_id(badge);
        // not sus
        seL4_Word target_chan = rrer_source_ch_to_target_ch(source_chan);

        unit.unit_type = rr_storage_unit_NTFN;
        unit.args.ntfn_args = (rr_storage_ntfn_args_t) {
            .badge = badge,
            .msginfo = msg,
            .source_chan = source_chan,
            .target_chan = target_chan, // uwu
        };
    } break;
    }
    // write the instruction.
    rr_storage_unit_print(&unit);
    rr_storage_write(handle->metadata.inst_end, (const uint8_t *)&unit, sizeof(unit));
    handle->metadata.inst_end += sizeof(unit);
    handle->metadata.inst_num++;
    rr_storage_update_metadata(handle);
}

// We store scheduler events
static inline void rr_storage_store_scheduler_event(rr_storage_handle_t *handle, seL4_Word cycle_count,
                                                    seL4_Word child_id, rr_ChildState_e new_state)
{
    rr_storage_unit_t unit = {
        .cycle_count = cycle_count,
        .unit_type = rr_storage_unit_SCHED,
        .args.sched_args =
            (rr_storage_sched_args_t) {
                .child = child_id,
                .state = new_state,
            },
    };
    // write the instruction.
    rr_storage_unit_print(&unit);
    rr_storage_write(handle->metadata.inst_end, (const uint8_t *)&unit, sizeof(unit));
    handle->metadata.inst_end += sizeof(unit);
    handle->metadata.inst_num++;
    rr_storage_update_metadata(handle);
}

// gets the current instruction without incrementing the iptr.
static inline rr_storage_unit_t rr_storage_get_inst(const rr_storage_metadata_t *metadata, seL4_Word iptr)
{
    rr_storage_unit_t inst = { 0 };
    assert(iptr < metadata->inst_end);
    INFO("iptr: %lx\n", iptr);
    STR_NO_ERR(rr_storage_read(iptr, (uint8_t *)&inst, sizeof(inst)));
    return inst;
}

// gets the instruction that the iptr is currently pointing to and then increments iptr.
// iptr always points to the NEXT instruction!
static inline rr_storage_unit_t rr_storage_get_next_inst(rr_storage_handle_t *handle)
{
    rr_storage_unit_t inst = rr_storage_get_inst(&handle->metadata, handle->iptr);
    handle->iptr+= sizeof(rr_storage_unit_t);
    return inst;
}

static inline void rr_storage_unit_print(const rr_storage_unit_t *unit)
{
    switch (unit->unit_type) {
    case rr_storage_unit_REPLY: {
        REC("0x%lx REPLY source_child:%lu target_child:%lu msginfo:0x%lx badge:0x%lx data_offset:0x%lx\n",
            unit->cycle_count, unit->args.reply_args.source_child, unit->args.reply_args.target_child,
            unit->args.reply_args.msginfo.words[0], unit->args.reply_args.badge, unit->args.reply_args.data_offset);
    } break;
    case rr_storage_unit_MSG: {
        REC("0x%lx MSG source_chan:%lu target_chan:%lu msginfo:0x%lx badge:0x%lx data_offset:0x%lx\n",
            unit->cycle_count, unit->args.msg_args.source_chan, unit->args.msg_args.target_chan,
            unit->args.msg_args.msginfo.words[0], unit->args.msg_args.badge, unit->args.msg_args.data_offset);
    } break;
    case rr_storage_unit_CALL: {
        REC("0x%lx CALL source_chan:%lu target_chan:%lu msginfo:0x%lx badge:0x%lx data_offset:0x%lx\n",
            unit->cycle_count, unit->args.call_args.source_chan, unit->args.call_args.target_chan,
            unit->args.call_args.msginfo.words[0], unit->args.call_args.badge, unit->args.call_args.data_offset);
    } break;
    case rr_storage_unit_NTFN: {
        REC("0x%lx NTFN source_chan:%lu target_chan:%lu msginfo:0x%lx badge:0x%lx\n", unit->cycle_count,
            unit->args.ntfn_args.source_chan, unit->args.ntfn_args.target_chan, unit->args.ntfn_args.msginfo.words[0],
            unit->args.ntfn_args.badge);
    } break;
    case rr_storage_unit_SCHED: {
        REC("0x%lx SCHED child:%lu new_state:%s\n", unit->cycle_count, unit->args.sched_args.child,
            rr_child_state_to_string(unit->args.sched_args.state));
    } break;

    case _rr_storage_unit_max:
    default:
        assert(!"Unreachable");
    }
}
