/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * A manager PD with real seL4 authority over a sandbox PD.
 *
 * Through <cspace> in the system description, the manager holds an Untyped,
 * its own root CNode, VSpace and TCB, the sandbox's root CNode and VSpace,
 * and the frames of a surface that the display PD shows. At run time it
 * makes frames, page tables and a notification from the Untyped, loads a
 * program into the sandbox, grants it the notification and the surface, and
 * starts it.
 *
 * Revoking the surface unmaps it from the sandbox, and the kernel stops the
 * program at its next write to it, while the display keeps its own mapping.
 * Revoking the Untyped destroys everything made from it; the Untyped is
 * then reused for the next program. Revoking just the notification takes
 * away that one capability: the program runs on, but its signals no longer
 * reach the manager.
 */

#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>
#include "layout.h"

/* Root CNode slots filled by the system description */
#define SLOT_UNTYPED 1
#define SLOT_SANDBOX_CNODE 2
#define SLOT_SANDBOX_VSPACE 3
#define SLOT_OWN_VSPACE 4
#define SLOT_OWN_CNODE 5
#define SLOT_OWN_TCB 6
/* A CNode with a cap to each frame of the surface; (7 << 58) | i is frame i */
#define SLOT_SURFACE 7
/* Free root CNode slots for objects made at run time */
#define SLOT_FIRST_FREE 8
#define SLOT_LAST 63

#define ROOT_BITS 6
#define SLOT_DEPTH (seL4_WordBits - ROOT_BITS)
/* The cap in root slot s, as a CPtr, resolving any CNode cap it holds further */
#define CPTR(s) ((seL4_CPtr)(s) << SLOT_DEPTH)
/* The cap in root slot s, as a leaf, via the CNode cap to our own root CNode */
#define LEAF(s) (CPTR(SLOT_OWN_CNODE) | (s))
/* Our root CNode, to address our slots with depth SLOT_DEPTH */
#define SELF LEAF(SLOT_OWN_CNODE)

#define PAGE_SIZE 0x1000
/* Where the manager maps pages of the program to fill them */
#define WINDOW_VADDR 0x60000000UL
/* Where the manager sees the page it shares with the program */
#define SHARED_VADDR 0x60100000UL

#define SANDBOX 0
#define DISPLAY_CH 1
/* As in dynamic_caps.system */
#define MANAGER_PRIORITY 100
#define SANDBOX_PRIORITY 50

extern char _program[], _program_end[];

static unsigned next_slot;
/* Our copies of the surface's frame caps, mapped into the sandbox */
static unsigned surface_slots[SURFACE_PAGES];
static seL4_CPtr ntfn_slot;
static bool done;

static void say(const char *s)
{
    microkit_dbg_puts(s);
}

static void puthex(seL4_Word x)
{
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        buf[2 + i] = "0123456789abcdef"[(x >> (60 - 4 * i)) & 0xf];
    }
    buf[18] = '\0';
    say(buf);
}

static void putdec(seL4_Word x)
{
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    do {
        buf[--i] = '0' + x % 10;
        x /= 10;
    } while (x);
    say(buf + i);
}

static void check(seL4_Error err, const char *what)
{
    if (err != seL4_NoError) {
        say("MANAGER: ");
        say(what);
        say(" failed with error ");
        putdec(err);
        say("\n");
        for (;;) {
        }
    }
}

static unsigned alloc_slot(void)
{
    if (next_slot > SLOT_LAST) {
        say("MANAGER: out of CSpace slots\n");
        for (;;) {
        }
    }
    return next_slot++;
}

/* Make one object of `type` from the Untyped, in a fresh root CNode slot */
static unsigned retype(seL4_Word type, const char *what)
{
    unsigned slot = alloc_slot();
    check(seL4_Untyped_Retype(CPTR(SLOT_UNTYPED), type, 0, SELF, 0, 0, slot, 1), what);
    return slot;
}

/* A second cap to the object in `src`, for mapping it into a second VSpace */
static unsigned copy(unsigned src, seL4_CapRights_t rights)
{
    unsigned slot = alloc_slot();
    check(seL4_CNode_Copy(SELF, slot, SLOT_DEPTH, SELF, src, SLOT_DEPTH, rights), "copying a cap");
    return slot;
}

/* Map the frame in `frame` at `vaddr` in `vspace`, making page tables as needed */
static void map_page(unsigned frame, unsigned vspace, seL4_Word vaddr, seL4_CapRights_t rights,
                     seL4_ARM_VMAttributes attrs)
{
    for (;;) {
        seL4_Error err = seL4_ARM_Page_Map(LEAF(frame), LEAF(vspace), vaddr, rights, attrs);
        if (err != seL4_FailedLookup) {
            check(err, "mapping a page");
            return;
        }
        /* A page table is missing at some level */
        unsigned pt = retype(seL4_ARM_PageTableObject, "making a page table");
        check(seL4_ARM_PageTable_Map(LEAF(pt), LEAF(vspace), vaddr, seL4_ARM_Default_VMAttributes),
              "mapping a page table");
    }
}

/*
 * Map the surface into the sandbox, through copies of the frame caps from
 * the system description. Those caps are themselves copies made by the
 * CapDL initialiser, not originals, so seL4_CNode_Revoke on them would not
 * reach our copies: we keep track of the copies and delete them instead.
 */
static void grant_surface(void)
{
    for (int i = 0; i < SURFACE_PAGES; i++) {
        unsigned slot = alloc_slot();
        check(seL4_CNode_Copy(SELF, slot, SLOT_DEPTH, LEAF(SLOT_SURFACE), i, SLOT_DEPTH, seL4_ReadWrite),
              "copying a frame of the surface");
        map_page(slot, SLOT_SANDBOX_VSPACE, PROGRAM_SURFACE_VADDR + i * PAGE_SIZE, seL4_ReadWrite,
                 seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever);
        surface_slots[i] = slot;
    }
}

/* Delete our copies of the surface's frame caps, which unmaps them from the sandbox */
static void revoke_surface(void)
{
    for (int i = 0; i < SURFACE_PAGES; i++) {
        if (surface_slots[i]) {
            check(seL4_CNode_Delete(SELF, surface_slots[i], SLOT_DEPTH), "deleting a frame of the surface");
            surface_slots[i] = 0;
        }
    }
}

/* Ask the display what the surface shows; it runs at a higher priority */
static void show(void)
{
    microkit_notify(DISPLAY_CH);
}

/* Load the program into the sandbox, grant it a notification and the surface, and start it */
static void spawn(void)
{
    seL4_Word size = _program_end - _program;
    seL4_Word pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages > PROGRAM_MAX_PAGES) {
        say("MANAGER: program too large\n");
        return;
    }
    next_slot = SLOT_FIRST_FREE;

    /* The code: fill each frame through our window, then map it read-only */
    for (seL4_Word i = 0; i < pages; i++) {
        unsigned frame = retype(seL4_ARM_SmallPageObject, "making a frame");
        unsigned mine = copy(frame, seL4_ReadWrite);
        map_page(mine, SLOT_OWN_VSPACE, WINDOW_VADDR, seL4_ReadWrite, seL4_ARM_Default_VMAttributes);
        /* volatile, so the compiler does not call a memcpy we lack */
        volatile char *dst = (volatile char *)WINDOW_VADDR;
        for (seL4_Word j = 0; j < PAGE_SIZE; j++) {
            seL4_Word off = i * PAGE_SIZE + j;
            dst[j] = off < size ? _program[off] : 0;
        }
        check(seL4_ARM_Page_Clean_Data(LEAF(mine), 0, PAGE_SIZE), "cleaning a page");
        check(seL4_ARM_Page_Unmap(LEAF(mine)), "unmapping a page");
        map_page(frame, SLOT_SANDBOX_VSPACE, PROGRAM_VADDR + i * PAGE_SIZE, seL4_CanRead,
                 seL4_ARM_Default_VMAttributes);
        check(seL4_ARM_Page_Unify_Instruction(LEAF(frame), 0, PAGE_SIZE), "unifying a page");
    }

    /* The stack, and a page shared with us, neither executable */
    unsigned stack = retype(seL4_ARM_SmallPageObject, "making a frame");
    map_page(stack, SLOT_SANDBOX_VSPACE, PROGRAM_STACK_VADDR, seL4_ReadWrite, seL4_ARM_ExecuteNever);
    unsigned shared = retype(seL4_ARM_SmallPageObject, "making a frame");
    map_page(shared, SLOT_SANDBOX_VSPACE, PROGRAM_SHARED_VADDR, seL4_ReadWrite, seL4_ARM_ExecuteNever);
    unsigned shared_mine = copy(shared, seL4_ReadWrite);
    map_page(shared_mine, SLOT_OWN_VSPACE, SHARED_VADDR, seL4_ReadWrite,
             seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever);

    /* A notification we wait on; the sandbox gets a send-only badged copy */
    ntfn_slot = retype(seL4_NotificationObject, "making a notification");
    check(seL4_CNode_Mint(LEAF(SLOT_SANDBOX_CNODE), SANDBOX_NTFN_SLOT, SLOT_DEPTH, SELF, ntfn_slot, SLOT_DEPTH,
                          seL4_CanWrite, 1),
          "granting the notification");

    grant_surface();

    say("MANAGER: made ");
    putdec(next_slot - SLOT_FIRST_FREE);
    say(" objects from the untyped, granted the surface, starting the program\n");
    microkit_pd_restart(SANDBOX, PROGRAM_VADDR);
}

static void wait_for_signals(int n)
{
    volatile uint64_t *counter = (volatile uint64_t *)SHARED_VADDR;
    for (int i = 0; i < n; i++) {
        seL4_Word badge;
        seL4_Wait(LEAF(ntfn_slot), &badge);
        say("MANAGER: signal with badge ");
        putdec(badge);
        say(", the program's counter is ");
        putdec(*counter);
        say("\n");
    }
}

/*
 * Let the program run on for a while and check that its signals no longer
 * arrive. Its slot for the notification is now empty, and seL4 drops a
 * signal to an empty slot (a debug kernel says so on the console). We run
 * the sandbox at our priority and yield to it, since it never blocks.
 */
static void watch_without_signals(void)
{
    volatile uint64_t *counter = (volatile uint64_t *)SHARED_VADDR;
    seL4_Word badge;
    seL4_Poll(LEAF(ntfn_slot), &badge);
    uint64_t start = *counter;
    check(seL4_TCB_SetPriority(BASE_TCB_CAP + SANDBOX, LEAF(SLOT_OWN_TCB), MANAGER_PRIORITY), "raising the sandbox");
    int signals = 0;
    while (*counter < start + 3) {
        seL4_Yield();
        seL4_Poll(LEAF(ntfn_slot), &badge);
        signals += badge != 0;
    }
    check(seL4_TCB_SetPriority(BASE_TCB_CAP + SANDBOX, LEAF(SLOT_OWN_TCB), SANDBOX_PRIORITY), "lowering the sandbox");
    say("MANAGER: the program counted from ");
    putdec(start);
    say(" to ");
    putdec(*counter);
    say(" and ");
    putdec(signals);
    say(" signals reached us\n");
}

/* Take back the surface, and destroy every object made from the Untyped, wherever its caps are */
static void revoke_all(void)
{
    revoke_surface();
    check(seL4_CNode_Revoke(SELF, SLOT_UNTYPED, SLOT_DEPTH), "revoking the untyped");
    say("MANAGER: revoked everything: the program's memory, page tables, notification and surface are gone\n");
}

void init(void)
{
    say("MANAGER: round 1: spawning\n");
    spawn();
    wait_for_signals(3);
    show();
    revoke_surface();
    say("MANAGER: revoked only the surface\n");
}

void notified(microkit_channel ch)
{
    (void)ch;
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    (void)reply_msginfo;
    seL4_Word label = microkit_msginfo_get_label(msginfo);

    say("MANAGER: the kernel reports a ");
    if (label == seL4_Fault_VMFault) {
        say("VM fault in the sandbox: ip ");
        puthex(seL4_GetMR(seL4_VMFault_IP));
        say(", address ");
        puthex(seL4_GetMR(seL4_VMFault_Addr));
        say(seL4_GetMR(seL4_VMFault_PrefetchFault) ? " (instruction fetch)\n" : " (data)\n");
    } else if (label == seL4_Fault_CapFault) {
        say("cap fault in the sandbox: ip ");
        puthex(seL4_GetMR(seL4_CapFault_IP));
        say(", cptr ");
        puthex(seL4_GetMR(seL4_CapFault_Addr));
        say("\n");
    } else {
        say("fault with label ");
        putdec(label);
        say(" in the sandbox\n");
    }

    /* Round 1 ends with the program writing to the surface it lost */
    if (label != seL4_Fault_VMFault || seL4_GetMR(seL4_VMFault_Addr) != PROGRAM_SURFACE_VADDR || done) {
        microkit_pd_stop(child);
        say("MANAGER: FAILED\n");
        return seL4_False;
    }
    done = true;

    revoke_all();

    say("MANAGER: round 2: spawning again from the same untyped\n");
    spawn();
    wait_for_signals(2);
    show();
    check(seL4_CNode_Revoke(SELF, ntfn_slot, SLOT_DEPTH), "revoking the notification");
    say("MANAGER: revoked only the sandbox's copy of the notification\n");
    watch_without_signals();

    microkit_pd_stop(child);
    revoke_all();
    show();
    say("MANAGER: DONE\n");
    return seL4_False;
}
