/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * The host's side of running apps in sandboxes (see sandbox.h).
 *
 * Through <cspace> in the system description (see meta.py), the host holds
 * its own root CNode and VSpace, a small Untyped for its own bookkeeping,
 * and for each sandbox an Untyped, the sandbox's VSpace, and the frames of
 * the surface and state of its window. At start-up it makes, for each
 * sandbox, a CNode with room for every object of that sandbox. For each app
 * it retypes frames and page tables from the sandbox's Untyped into that
 * CNode, fills them through a window in its own VSpace, and maps them into
 * the sandbox. Stopping the app revokes the Untyped, which deletes all of
 * that at once.
 *
 * The frame caps of the window come from the system description. They are
 * copies made by the CapDL initialiser, so revoking them would not reach
 * the host's copies: the host deletes the copies it granted instead.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <elf.h>
#include <microkit.h>
#include <sddf/util/util.h>
#include "sandbox_host.h"

/* Root CNode slots filled by the system description (see meta.py) */
#define SLOT_OWN_VSPACE 1
#define SLOT_OWN_CNODE 2
#define SLOT_BOOK_UNTYPED 3
/* Those of sandbox k start at SLOT_SANDBOX(k) */
#define SLOT_SANDBOX(k) (8 + 8 * (k))
#define SB_UNTYPED 0
#define SB_VSPACE 1
#define SB_SURFACE 2
#define SB_STATE 3
/* Filled at start-up: the CNode for the sandbox's objects, as retyped and with a guard */
#define SB_OBJECTS_RAW 4
#define SB_OBJECTS 5
/* Filled at start-up: page tables for our own mappings of sandboxes' frames */
#define SLOT_HOST_TABLES 40
#define SLOT_HOST_TABLES_END 64

_Static_assert(SLOT_SANDBOX(SANDBOX_COUNT) <= SLOT_HOST_TABLES, "too many sandboxes for the host's root CNode");

#define ROOT_BITS 6
#define SLOT_DEPTH (seL4_WordBits - ROOT_BITS)
/* The cap in root slot s, for caps other than CNodes */
#define CPTR(s) ((seL4_CPtr)(s) << SLOT_DEPTH)
/* The cap in root slot s as a leaf, even if it is a CNode, via the cap to our own root CNode */
#define LEAF(s) (CPTR(SLOT_OWN_CNODE) | (s))
#define SELF LEAF(SLOT_OWN_CNODE)

/* The CNode of a sandbox's objects: 2^OBJECT_BITS slots */
#define OBJECT_BITS 12
#define MAX_OBJECTS (1u << OBJECT_BITS)
#define OBJECTS_GUARD (seL4_WordBits - ROOT_BITS - OBJECT_BITS)

/* Where the host maps a sandbox's frames to fill them, and its view of each mailbox */
#define HOST_WINDOW_VADDR 0x50000000UL
#define HOST_WINDOW_SIZE 0x200000UL
#define HOST_MAILBOX_VADDR(k) (0x50400000UL + (k) * 0x200000UL)

#define PAGE SANDBOX_PAGE_SIZE
#define PAGES(bytes) (((bytes) + PAGE - 1) / PAGE)
#define RETYPE_BATCH 256

/* The largest window, see GUI_SURFACE_REGION_SIZE */
#define WINDOW_MAX_BYTES 0x100000UL
#define WINDOW_MAX_FRAMES (PAGES(WINDOW_MAX_BYTES) + 1)

#define LOG_SANDBOX_ERR(...) printf("WASM HOST|ERROR: sandbox: " __VA_ARGS__)

extern const char _runner_elf[], _runner_elf_end[];

typedef struct sandbox {
    unsigned base;
    bool started;
    uint32_t next_object;
    sandbox_mailbox_t *mailbox;
    /* What backs each grant, to take it back on cap_drop */
    struct {
        uint32_t first;
        uint32_t count;
    } grant_frames[CAPS_MAX];
    /* Our copies of the frame caps of the window, in the order they were granted.
       Only frames are listed: the page tables map_frame makes for the sandbox's
       VSpace come from its Untyped, so revoking that takes them with it. */
    uint32_t window_objects[WINDOW_MAX_FRAMES];
    uint32_t window_count;
    /* Our copy of the mailbox frame, mapped into our own VSpace; unmapped on stop */
    uint32_t mailbox_object;
    bool mailbox_mapped;
} sandbox_t;

static sandbox_t sandboxes[SANDBOX_COUNT];

/* The object in slot i of a sandbox's object CNode, and that CNode as a leaf */
static seL4_CPtr obj(sandbox_t *sb, uint32_t i)
{
    return CPTR(sb->base + SB_OBJECTS) | i;
}

static seL4_CPtr objects(sandbox_t *sb)
{
    return LEAF(sb->base + SB_OBJECTS);
}

static bool ok(seL4_Error err, const char *what)
{
    if (err != seL4_NoError) {
        LOG_SANDBOX_ERR("%s failed with error %d\n", what, (int)err);
        return false;
    }
    return true;
}

/* Retype `n` objects of `type` into consecutive slots; returns the first, or -1 */
static int64_t make_objects(sandbox_t *sb, seL4_Word type, uint32_t n)
{
    if (n > MAX_OBJECTS - sb->next_object) {
        LOG_SANDBOX_ERR("out of object slots\n");
        return -1;
    }
    uint32_t first = sb->next_object;
    for (uint32_t done = 0; done < n;) {
        uint32_t batch = MIN(n - done, (uint32_t)RETYPE_BATCH);
        if (!ok(seL4_Untyped_Retype(CPTR(sb->base + SB_UNTYPED), type, 0, objects(sb), 0, 0, first + done, batch),
                "retyping the untyped")) {
            return -1;
        }
        done += batch;
    }
    sb->next_object += n;
    return first;
}

/*
 * Map the frame in object slot `frame` at `vaddr` of the VSpace in root slot
 * `vspace`. Page tables missing in a sandbox's VSpace are made from its
 * Untyped. Ours were all made at start-up from the bookkeeping Untyped:
 * one made from a sandbox's Untyped would vanish when that sandbox stops,
 * with whatever else we had mapped through it.
 */
static bool map_frame(sandbox_t *sb, uint32_t frame, unsigned vspace, seL4_Word vaddr, seL4_CapRights_t rights,
                      seL4_ARM_VMAttributes attrs)
{
    for (;;) {
        seL4_Error err = seL4_ARM_Page_Map(obj(sb, frame), CPTR(vspace), vaddr, rights, attrs);
        if (err != seL4_FailedLookup || vspace == SLOT_OWN_VSPACE) {
            return ok(err, "mapping a frame");
        }
        int64_t pt = make_objects(sb, seL4_ARM_PageTableObject, 1);
        if (pt < 0
            || !ok(seL4_ARM_PageTable_Map(obj(sb, pt), CPTR(vspace), vaddr, seL4_ARM_Default_VMAttributes),
                   "mapping a page table")) {
            return false;
        }
    }
}

static bool map_frames(sandbox_t *sb, uint32_t first, uint32_t n, unsigned vspace, seL4_Word vaddr,
                       seL4_CapRights_t rights, seL4_ARM_VMAttributes attrs)
{
    for (uint32_t i = 0; i < n; i++) {
        if (!map_frame(sb, first + i, vspace, vaddr + i * PAGE, rights, attrs)) {
            return false;
        }
    }
    return true;
}

static void unmap_frames(sandbox_t *sb, uint32_t first, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        seL4_ARM_Page_Unmap(obj(sb, first + i));
    }
}

/* Map `n` frames in the sandbox with no execute rights */
static bool map_data(sandbox_t *sb, uint32_t first, uint32_t n, seL4_Word vaddr, seL4_CapRights_t rights)
{
    return map_frames(sb, first, n, sb->base + SB_VSPACE, vaddr, rights,
                      seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever);
}

/* Make `n` frames and map them into our window; returns the first, or -1 */
static int64_t make_filled_frames(sandbox_t *sb, uint32_t n)
{
    if (n * PAGE > HOST_WINDOW_SIZE) {
        LOG_SANDBOX_ERR("region too large\n");
        return -1;
    }
    int64_t first = make_objects(sb, seL4_ARM_SmallPageObject, n);
    if (first < 0
        || !map_frames(sb, first, n, SLOT_OWN_VSPACE, HOST_WINDOW_VADDR, seL4_ReadWrite,
                       seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever)) {
        return -1;
    }
    return first;
}

/* Load the runner's segments into the sandbox; returns its entry point, or 0 */
static seL4_Word load_runner(sandbox_t *sb)
{
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)_runner_elf;
    size_t elf_size = _runner_elf_end - _runner_elf;
    if (elf_size < sizeof(*eh) || memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_phentsize != sizeof(Elf64_Phdr)
        || eh->e_phoff + (uint64_t)eh->e_phnum * sizeof(Elf64_Phdr) > elf_size) {
        LOG_SANDBOX_ERR("the runner is not a valid ELF file\n");
        return 0;
    }

    seL4_Word mapped_end = SANDBOX_RUNNER_VADDR;
    for (int i = 0; i < eh->e_phnum; i++) {
        const Elf64_Phdr *ph = (const Elf64_Phdr *)(_runner_elf + eh->e_phoff) + i;
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) {
            continue;
        }
        seL4_Word start = ph->p_vaddr & ~(PAGE - 1);
        seL4_Word end = (ph->p_vaddr + ph->p_memsz + PAGE - 1) & ~(PAGE - 1);
        /* Segments must not share pages, see runner.ld */
        if (start < mapped_end || end > SANDBOX_RUNNER_VADDR + SANDBOX_RUNNER_MAX || ph->p_filesz > ph->p_memsz
            || ph->p_offset + ph->p_filesz > elf_size) {
            LOG_SANDBOX_ERR("bad segment in the runner\n");
            return 0;
        }
        mapped_end = end;

        uint32_t n = (end - start) / PAGE;
        int64_t first = make_filled_frames(sb, n);
        if (first < 0) {
            return 0;
        }
        /* Frames come zeroed from the kernel, which covers .bss */
        memcpy((char *)HOST_WINDOW_VADDR + (ph->p_vaddr - start), _runner_elf + ph->p_offset, ph->p_filesz);
        bool exec = ph->p_flags & PF_X;
        if (exec) {
            for (uint32_t j = 0; j < n; j++) {
                seL4_ARM_Page_Clean_Data(obj(sb, first + j), 0, PAGE);
            }
        }
        unmap_frames(sb, first, n);

        seL4_CapRights_t rights = (ph->p_flags & PF_W) ? seL4_ReadWrite : seL4_CanRead;
        seL4_ARM_VMAttributes attrs = seL4_ARM_Default_VMAttributes | (exec ? 0 : seL4_ARM_ExecuteNever);
        if (!map_frames(sb, first, n, sb->base + SB_VSPACE, start, rights, attrs)) {
            return 0;
        }
        if (exec) {
            for (uint32_t j = 0; j < n; j++) {
                seL4_ARM_Page_Unify_Instruction(obj(sb, first + j), 0, PAGE);
            }
        }
    }
    return eh->e_entry;
}

/* Read `size` bytes of `path` into new frames mapped in the sandbox at `vaddr` with `rights` */
static int64_t load_file(sandbox_t *sb, sandbox_read_fn read, const char *path, uint64_t size, seL4_Word vaddr,
                         seL4_CapRights_t rights)
{
    uint32_t n = MAX(PAGES(size), 1);
    int64_t first = make_filled_frames(sb, n);
    if (first < 0) {
        return -1;
    }
    int64_t got = read(path, 0, (uint8_t *)HOST_WINDOW_VADDR, size);
    unmap_frames(sb, first, n);
    if (got != (int64_t)size) {
        LOG_SANDBOX_ERR("could not read %s\n", path);
        return -1;
    }
    return map_data(sb, first, n, vaddr, rights) ? first : -1;
}

/* Map copies of the first `n` frames of the region whose frame caps are in root slot `slot` */
static bool grant_region(sandbox_t *sb, unsigned slot, uint32_t n, seL4_Word vaddr)
{
    /* Check up front, so a window that cannot fit does not leave the object count
       past the end part way through the loop. */
    if (n > WINDOW_MAX_FRAMES - sb->window_count || n > MAX_OBJECTS - sb->next_object) {
        LOG_SANDBOX_ERR("the window does not fit\n");
        return false;
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t copy = sb->next_object++;
        if (!ok(seL4_CNode_Copy(objects(sb), copy, SLOT_DEPTH, LEAF(slot), i, SLOT_DEPTH, seL4_ReadWrite),
                "copying a frame of the window")) {
            return false;
        }
        sb->window_objects[sb->window_count++] = copy;
        if (!map_data(sb, copy, 1, vaddr + i * PAGE, seL4_ReadWrite)) {
            return false;
        }
    }
    return true;
}

/* Unmap the window's frames from the sandbox before deleting their caps: deleting a
   cap to a frame that is still mapped leaves the mapping alive, so the next app in
   this sandbox would see this one's pixels and mapping over them would fail. */
static void revoke_window(sandbox_t *sb)
{
    for (uint32_t i = 0; i < sb->window_count; i++) {
        seL4_ARM_Page_Unmap(obj(sb, sb->window_objects[i]));
        seL4_CNode_Delete(objects(sb), sb->window_objects[i], SLOT_DEPTH);
    }
    sb->window_count = 0;
}

/* Make every page table our VSpace lacks to map a page at `vaddr`, from the bookkeeping Untyped */
static bool make_host_tables(seL4_Word vaddr, unsigned *next_slot)
{
    for (;;) {
        if (*next_slot >= SLOT_HOST_TABLES_END) {
            LOG_SANDBOX_ERR("out of slots for page tables\n");
            return false;
        }
        unsigned slot = *next_slot;
        if (!ok(seL4_Untyped_Retype(CPTR(SLOT_BOOK_UNTYPED), seL4_ARM_PageTableObject, 0, SELF, 0, 0, slot, 1),
                "making a page table")) {
            return false;
        }
        seL4_Error err = seL4_ARM_PageTable_Map(CPTR(slot), CPTR(SLOT_OWN_VSPACE), vaddr, seL4_ARM_Default_VMAttributes);
        if (err == seL4_DeleteFirst) {
            /* Every level is there already */
            seL4_CNode_Delete(SELF, slot, SLOT_DEPTH);
            return true;
        }
        if (!ok(err, "mapping a page table")) {
            return false;
        }
        (*next_slot)++;
    }
}

bool sandbox_init(void)
{
    unsigned next_slot = SLOT_HOST_TABLES;
    for (seL4_Word vaddr = HOST_WINDOW_VADDR; vaddr < HOST_WINDOW_VADDR + HOST_WINDOW_SIZE; vaddr += 0x200000) {
        if (!make_host_tables(vaddr, &next_slot)) {
            return false;
        }
    }
    for (int k = 0; k < SANDBOX_COUNT; k++) {
        if (!make_host_tables(HOST_MAILBOX_VADDR(k), &next_slot)) {
            return false;
        }
    }

    for (int k = 0; k < SANDBOX_COUNT; k++) {
        sandbox_t *sb = &sandboxes[k];
        sb->base = SLOT_SANDBOX(k);
        sb->mailbox = (sandbox_mailbox_t *)HOST_MAILBOX_VADDR(k);
        /* The CNode for the sandbox's objects, with a guard so that obj(sb, i) names slot i */
        if (!ok(seL4_Untyped_Retype(CPTR(SLOT_BOOK_UNTYPED), seL4_CapTableObject, OBJECT_BITS, SELF, 0, 0,
                                    sb->base + SB_OBJECTS_RAW, 1),
                "making an object CNode")
            || !ok(seL4_CNode_Mint(SELF, sb->base + SB_OBJECTS, SLOT_DEPTH, SELF, sb->base + SB_OBJECTS_RAW,
                                   SLOT_DEPTH, seL4_AllRights, seL4_CNode_CapData_new(0, OBJECTS_GUARD).words[0]),
                   "setting the guard of an object CNode")) {
            return false;
        }
    }
    return true;
}

void sandbox_stop(int k)
{
    sandbox_t *sb = &sandboxes[k];
    if (!sb->started) {
        return;
    }
    microkit_pd_stop(k);
    revoke_window(sb);
    /* Our copy of the mailbox is mapped into our own VSpace, so unmap it before the
       Untyped takes the frame away. Otherwise the mapping outlives the frame, and the
       next app in this sandbox could not map a new mailbox over it. */
    if (sb->mailbox_mapped) {
        seL4_ARM_Page_Unmap(obj(sb, sb->mailbox_object));
        sb->mailbox_mapped = false;
    }
    /* Everything else was made from the Untyped */
    ok(seL4_CNode_Revoke(SELF, sb->base + SB_UNTYPED, SLOT_DEPTH), "revoking a sandbox's untyped");
    sb->next_object = 0;
    sb->started = false;
}

bool sandbox_start(int k, const char *name, const char *module_path, uint64_t module_size, caps_table_t *caps,
                   uint32_t width, uint32_t height, sandbox_read_fn read, char *error, size_t error_len)
{
    sandbox_t *sb = &sandboxes[k];
    sandbox_stop(k);
    sb->started = true;
    sb->next_object = 0;
    sb->window_count = 0;
    /* Cleared by sandbox_stop() above, set again once the mailbox is mapped */
    sb->mailbox_mapped = false;
    memset(sb->grant_frames, 0, sizeof(sb->grant_frames));
    snprintf(error, error_len, "could not build the sandbox");

    if (module_size == 0 || module_size > SANDBOX_MODULE_MAX) {
        snprintf(error, error_len, "module too large");
        goto fail;
    }

    seL4_Word entry = load_runner(sb);
    if (!entry) {
        goto fail;
    }

    int64_t stack = make_objects(sb, seL4_ARM_SmallPageObject, PAGES(SANDBOX_STACK_SIZE));
    int64_t heap = make_objects(sb, seL4_ARM_SmallPageObject, PAGES(SANDBOX_HEAP_SIZE));
    if (stack < 0 || heap < 0 || !map_data(sb, stack, PAGES(SANDBOX_STACK_SIZE), SANDBOX_STACK_VADDR, seL4_ReadWrite)
        || !map_data(sb, heap, PAGES(SANDBOX_HEAP_SIZE), SANDBOX_HEAP_VADDR, seL4_ReadWrite)) {
        goto fail;
    }

    /* WAMR's loader rewrites parts of the module in place; it is the sandbox's own copy */
    if (load_file(sb, read, module_path, module_size, SANDBOX_MODULE_VADDR, seL4_ReadWrite) < 0) {
        snprintf(error, error_len, "could not read the module");
        goto fail;
    }

    /* The mailbox, which we map too */
    int64_t box = make_objects(sb, seL4_ARM_SmallPageObject, 1);
    if (box < 0 || !map_data(sb, box, 1, SANDBOX_MAILBOX_VADDR, seL4_ReadWrite)) {
        goto fail;
    }
    uint32_t box_copy = sb->next_object++;
    if (box_copy >= MAX_OBJECTS
        || !ok(seL4_CNode_Copy(objects(sb), box_copy, SLOT_DEPTH, objects(sb), box, SLOT_DEPTH, seL4_ReadWrite),
               "copying the mailbox")
        || !map_frame(sb, box_copy, SLOT_OWN_VSPACE, HOST_MAILBOX_VADDR(k), seL4_ReadWrite,
                      seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever)) {
        goto fail;
    }
    sb->mailbox_object = box_copy;
    sb->mailbox_mapped = true;

    /* What the app is granted: files and the window are mapped, the rest is described */
    sandbox_grants_t g = { .magic = SANDBOX_GRANTS_MAGIC, .module_size = module_size };
    strncpy(g.app_name, name, SANDBOX_NAME_MAX - 1);
    int files = 0;
    for (int i = 0; i < CAPS_MAX && i < SANDBOX_MAX_GRANTS; i++) {
        cap_t *cap = &caps->caps[i];
        if (!cap->live) {
            continue;
        }
        sandbox_grant_t *sg = &g.grants[i];
        sg->live = 1;
        sg->type = cap->type;
        strncpy(sg->name, cap->name, SANDBOX_NAME_MAX - 1);
        if (cap->type == CAP_FILE) {
            uint64_t size = MIN(cap->size, (uint64_t)SANDBOX_FILE_MAX);
            seL4_Word vaddr = SANDBOX_FILES_VADDR + files * SANDBOX_FILE_MAX;
            int64_t first = load_file(sb, read, cap->path, size, vaddr, seL4_CanRead);
            if (first < 0) {
                snprintf(error, error_len, "could not read %s", cap->path);
                goto fail;
            }
            sg->vaddr = vaddr;
            sg->size = size;
            sb->grant_frames[i].first = first;
            sb->grant_frames[i].count = MAX(PAGES(size), 1);
            files++;
        } else if (cap->type == CAP_WINDOW) {
            g.width = width;
            g.height = height;
            if ((uint64_t)width * height * 4 > WINDOW_MAX_BYTES
                || !grant_region(sb, sb->base + SB_SURFACE, PAGES((uint64_t)width * height * 4),
                                 SANDBOX_SURFACE_VADDR)
                || !grant_region(sb, sb->base + SB_STATE, 1, SANDBOX_STATE_VADDR)) {
                goto fail;
            }
        }
    }

    int64_t grants_frame = make_filled_frames(sb, 1);
    if (grants_frame < 0) {
        goto fail;
    }
    memcpy((void *)HOST_WINDOW_VADDR, &g, sizeof(g));
    unmap_frames(sb, grants_frame, 1);
    if (!map_data(sb, grants_frame, 1, SANDBOX_GRANTS_VADDR, seL4_CanRead)) {
        goto fail;
    }

    printf("WASM HOST|INFO: sandbox %d for %s: %u objects made, %d file(s) and %s mapped\n", k, name,
           sb->next_object, files, sb->window_count ? "the window" : "no window");
    microkit_pd_restart(k, entry);
    return true;

fail:
    sandbox_stop(k);
    return false;
}

void sandbox_revoke_grant(int k, int handle, cap_type_t type)
{
    sandbox_t *sb = &sandboxes[k];
    if (!sb->started || handle < 0 || handle >= CAPS_MAX) {
        return;
    }
    if (type == CAP_WINDOW) {
        revoke_window(sb);
    } else if (type == CAP_FILE) {
        unmap_frames(sb, sb->grant_frames[handle].first, sb->grant_frames[handle].count);
        sb->grant_frames[handle].count = 0;
    }
}

static bool post(int k, sandbox_msg_t msg)
{
    sandbox_t *sb = &sandboxes[k];
    if (!sb->started) {
        return false;
    }
    sandbox_mailbox_t *mb = sb->mailbox;
    uint32_t tail = mb->msg_tail;
    uint32_t head = __atomic_load_n(&mb->msg_head, __ATOMIC_ACQUIRE);
    /* The runner writes msg_head, so it may be anything; a full or corrupt queue just drops */
    if (tail - head >= SANDBOX_MSGS) {
        return false;
    }
    mb->msgs[tail % SANDBOX_MSGS] = msg;
    __atomic_store_n(&mb->msg_tail, tail + 1, __ATOMIC_RELEASE);
    return true;
}

bool sandbox_post_event(int k, gui_event_t ev)
{
    return post(k, (sandbox_msg_t) { .type = SANDBOX_MSG_EVENT, .ev = ev });
}

bool sandbox_post_tick(int k, uint32_t time_ms)
{
    return post(k, (sandbox_msg_t) { .type = SANDBOX_MSG_TICK, .time_ms = time_ms });
}

void sandbox_kick(int k)
{
    if (sandboxes[k].started) {
        microkit_notify(SANDBOX_HOST_CH_BASE + k);
    }
}

bool sandbox_next_request(int k, sandbox_req_t *req)
{
    sandbox_t *sb = &sandboxes[k];
    if (!sb->started) {
        return false;
    }
    sandbox_mailbox_t *mb = sb->mailbox;
    uint32_t head = mb->req_head;
    uint32_t tail = __atomic_load_n(&mb->req_tail, __ATOMIC_ACQUIRE);
    if (tail == head) {
        return false;
    }
    if (tail - head > SANDBOX_REQS) {
        /* The runner wrote a nonsensical index: drop everything */
        __atomic_store_n(&mb->req_head, tail, __ATOMIC_RELEASE);
        return false;
    }
    *req = mb->reqs[head % SANDBOX_REQS];
    __atomic_store_n(&mb->req_head, head + 1, __ATOMIC_RELEASE);

    /* Keep only printable text */
    req->text[SANDBOX_TEXT_MAX - 1] = '\0';
    for (char *c = req->text; *c; c++) {
        if (*c < ' ' || *c > '~') {
            *c = '?';
        }
    }
    return true;
}
