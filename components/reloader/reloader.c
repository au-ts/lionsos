#include <sddf/serial/queue.h>
#include <microkit.h> 
#include <poll.h>
#include <sys/types.h>
#include <vspace.h>
#include <sel4/sel4.h>
#include <sddf/util/printf.h>

#include <stdint.h>
#include <stddef.h>

#define MAX_FRAMES 32

#include <string.h>
#include <assert.h>
#include <sys/param.h>
#include <math.h>

#include <stdarg.h>
#include "../include/arena.h"

#include <stdlib.h>
#include <sddf/network/queue.h>

// idr why here
uintptr_t small_mapping_mr = 0x900000;
uintptr_t large_mapping_mr = 0xa00000;
uintptr_t stack_map = 0x29000000;
uintptr_t Shared_Elf_Arena = 0x30000000;
char seg_buf[4096];

#define PAGE_SIZE_4K 0x1000

#define ROUND_DOWN(n,d) (d*(n/d))

#define MAX_PID 9 // this variable needs to be changed as more pd_id come in, we should probably change this a bit to make it better

    // the stack is only one page long anyway:
void backtrace(int pd_id, seL4_Word fp_vaddr) { // not sure how it will work when going over multiple pages
    uint64_t page_size;
    uint64_t base = ROUND_DOWN(fp_vaddr, PAGE_SIZE_4K);
    for (uint64_t vaddr = base; vaddr < seL4_UserTop; vaddr += PAGE_SIZE_4K) {
        seL4_Word page = get_page(pd_id, vaddr, &page_size);
        if (page == 0xffffffffffffffff) {
            sddf_dprintf("we did not have a valid page at this point!\n");
            return;
        }
        seL4_ARM_VSpace_CleanInvalidate_Data(BASE_VSPACE_CAP + pd_id, vaddr, vaddr + PAGE_SIZE_4K - 1);
        int err = seL4_ARM_Page_Map(page, VSPACE_CAP, stack_map + vaddr - base, seL4_AllRights, seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever);
        if (err) {
            sddf_dprintf("backtrace: failed to map stack page: %d with vaddr: 0x%lx\n", err, vaddr);
            return;
        }
    }

    uintptr_t *fp = (uintptr_t *)(stack_map + fp_vaddr - base);
    sddf_dprintf("BACKTRACE_START\n");
    for (int i = 0; i < MAX_FRAMES; i++) {
        if (fp == NULL) break;
        fp_vaddr = fp[0];
        uintptr_t lr = fp[1];
        uintptr_t *prev_fp = (uintptr_t *)(stack_map + fp[0] - base);
        if (lr == 0) break;
        sddf_dprintf("0x%lx ", lr);
        if (prev_fp == NULL || prev_fp <= fp) break;
        fp = prev_fp;
    }
    sddf_dprintf("\nBACKTRACE_END\n");
}

__attribute__((__section__(".reloading_dependencies"))) unsigned char d_matrix[MAX_PID][MAX_PID];

serial_queue_handle_t rx_queue_handle;
serial_queue_handle_t tx_queue_handle;

// this is used for serial, so there should be some channel
void notified(microkit_channel ch) {
    sddf_dprintf("we called the notified, likely from the serial, with channel: %d!\n", ch);

    // should be some shared memory region.
    char c;
    serial_dequeue(&rx_queue_handle, &c);
    sddf_dprintf("the char was: %c\n", c);
}

void init(void)
{
    microkit_dbg_puts("reloader is starting!\n");
    sddf_dprintf("we have that our user top is: 0x%lx", seL4_UserTop);

    libvspace_set_small_mapping_region(small_mapping_mr);
    libvspace_set_large_mapping_region(large_mapping_mr);

    serial_queue_init(&rx_queue_handle, (serial_queue_t *)0x32000000, 0x10000, (char *)0x32001000);
    serial_queue_init(&tx_queue_handle, (serial_queue_t *)0x32011000, 0x10000, (char *)0x32012000);
}


// we also need to have an internal pd map, that works with the webserver map
static int load_segment_into_vspace(int pd_id, const char *src, int segment_size,
                                    int file_size, uintptr_t dst)
{
    assert(file_size <= segment_size);
    unsigned int pos = 0;
    while (pos < segment_size) {
        uintptr_t loadee_vaddr = ROUND_DOWN(dst, PAGE_SIZE_4K);
        void *loader_data = seg_buf;

        /* Write any zeroes at the start of the block. */
        size_t leading_zeroes = dst % PAGE_SIZE_4K;
        memset(loader_data, 0, leading_zeroes);
        loader_data += leading_zeroes;

        /* Copy the data from the source. */
        size_t segment_bytes = PAGE_SIZE_4K - leading_zeroes;
        if (pos < file_size) {
            size_t file_bytes = MIN(segment_bytes, file_size - pos);
            memcpy(loader_data, src, file_bytes);
            loader_data += file_bytes;
            
            /* Fill in the end of the frame with zereos */
            size_t trailing_zeroes = PAGE_SIZE_4K - (leading_zeroes + file_bytes);
            memset(loader_data, 0, trailing_zeroes);
        } else {
            memset(loader_data, 0, segment_bytes);
        }

        libvspace_write_page(pd_id, loadee_vaddr, seg_buf, PAGE_SIZE_4K);

        pos += segment_bytes;
        dst += segment_bytes;
        src += segment_bytes;
    }

    return 0;
}

void stop_dependencies(int pd_id) {
    int j = 0;
    while (j < MAX_PID && d_matrix[pd_id][j] != 0) {
        microkit_pd_stop(d_matrix[pd_id][j]); // stops the main one as well
        j++;
    }
}

// what order should I be using for this?
// also assume all entry points are the same
void reload_dependencies(int pd_id, int entry_point) {
    int j = 0;
    while (j < MAX_PID && d_matrix[pd_id][j] != 0) {
        microkit_pd_reload(d_matrix[pd_id][j], entry_point, seL4_UserTop); // idrk where stack should be
        j++;
    }
}

int from_webserver(int pd_id) {
    int num_seg = get_num_segments((Arena *)Shared_Elf_Arena);
    segment *segments = get_segments((Arena *)Shared_Elf_Arena);
    for (int i = 0; i < num_seg; i++) {
        load_segment_into_vspace(pd_id, (char *)segments[i].src, segments[i].size, segments[i].size, segments[i].target_vaddr);
    }
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo) {
    sddf_dprintf("we called the protected procedure!\n");
    microkit_msginfo msg;
    int pd_id = microkit_mr_get(0);
    seL4_Word microkit_passive_vaddr = microkit_mr_get(1);
    int entry_point = microkit_mr_get(2); // to be honest I can assume this is identical as well, so I am sort of wasting space rn

    stop_dependencies(pd_id);

    // now I need to be able to distinguish by another argument
    if (from_webserver(pd_id)) {
        // this is bad
    };

    int microkit_passive;
    libvspace_read_word(pd_id, microkit_passive_vaddr, &microkit_passive);

    // I can just start from the top again and allocate away, Idk why I would need to zero out
    // memset(seg_buf, 0, PAGE_SIZE_4K);
    // libvspace_write_page(pd_id, seL4_UserTop - PAGE_SIZE_4K, seg_buf, PAGE_SIZE_4K);

    if (microkit_passive) { // give it back its scheduling context
        sddf_dprintf("We are calling into the monitor because we are a passive pd!\n");
        msg = microkit_msginfo_new(0, 1);
        microkit_mr_set(0, pd_id);
        seL4_Send(MONITOR_EP, msg);
    }
    reload_dependencies(pd_id, entry_point);

    msg = microkit_msginfo_new(0, 1);
    microkit_mr_set(0, 0);
    return msg;
}

// fault handler for stack unwiding
// assume we are using aarch64 for now
seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    sddf_dprintf("fault handler for reloader with pid: %d\n", child);

    seL4_UserContext regs;  
    int err = seL4_TCB_ReadRegisters(BASE_TCB_CAP + child, false, 0, sizeof(seL4_UserContext) / sizeof(seL4_Word), &regs); // idk if I have the index correct
    if (err) {
        sddf_dprintf("fault: failed to read registers: %d\n", err);
        return seL4_False;
    }

    sddf_dprintf("The pid is: %d", child);
    uintptr_t fp_vaddr = regs.x29;
    sddf_dprintf("the vaddr for the crash is: 0x%lx\n", fp_vaddr);
    backtrace(child, fp_vaddr);

    microkit_internal_crash(1);

    return seL4_False;
}