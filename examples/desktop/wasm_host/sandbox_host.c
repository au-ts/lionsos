/*
 * Copyright 2026, LionsOS Contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * The host's side of running apps in a sandbox (see sandbox.h).
 *
 * Through <cspace> in the system description (see meta.py), the host holds
 * an Untyped for the sandbox, its own root CNode and VSpace, the sandbox's
 * VSpace, the frames of its own surface and state regions, and a small
 * Untyped for its own bookkeeping. At start-up it makes a CNode with room
 * for every object of a sandbox. For each app it retypes frames and page
 * tables from the sandbox's Untyped into that CNode, fills them through a
 * window in its own VSpace, and maps them into the sandbox. Stopping the
 * app revokes the Untyped, which deletes all of that at once.
 *
 * The frame caps of the surface and state come from the system description.
 * They are copies made by the CapDL initialiser, so revoking them would not
 * reach the host's copies: the host deletes the copies it granted instead.
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
#define SLOT_UNTYPED 1
#define SLOT_SANDBOX_VSPACE 2
#define SLOT_OWN_VSPACE 3
#define SLOT_OWN_CNODE 4
#define SLOT_SURFACE 5
#define SLOT_STATE 6
#define SLOT_BOOK_UNTYPED 7
/* Filled at start-up: the CNode for the sandbox's objects, as retyped and with a guard */
#define SLOT_OBJECTS_RAW 8
#define SLOT_OBJECTS 9

#define ROOT_BITS 6
#define SLOT_DEPTH (seL4_WordBits - ROOT_BITS)
/* The cap in root slot s, for caps other than CNodes */
#define CPTR(s) ((seL4_CPtr)(s) << SLOT_DEPTH)
/* The cap in root slot s as a leaf, even if it is a CNode, via the cap to our own root CNode */
#define LEAF(s) (CPTR(SLOT_OWN_CNODE) | (s))
#define SELF LEAF(SLOT_OWN_CNODE)

/* The CNode of the sandbox's objects: 2^OBJECT_BITS slots, found at root slot SLOT_OBJECTS */
#define OBJECT_BITS 12
#define MAX_OBJECTS (1u << OBJECT_BITS)
#define OBJECTS_GUARD (seL4_WordBits - ROOT_BITS - OBJECT_BITS)
/* The object in slot i of that CNode */
#define OBJ(i) (CPTR(SLOT_OBJECTS) | (i))
#define OBJECTS LEAF(SLOT_OBJECTS)

/* Where the host maps the sandbox's frames to fill them, and its view of the mailbox */
#define HOST_WINDOW_VADDR 0x50000000UL
#define HOST_WINDOW_SIZE 0x200000UL
#define HOST_MAILBOX_VADDR 0x50400000UL

#define PAGE SANDBOX_PAGE_SIZE
#define PAGES(bytes) (((bytes) + PAGE - 1) / PAGE)
#define RETYPE_BATCH 256

#define LOG_SANDBOX_ERR(...) printf("WASM HOST|ERROR: sandbox: " __VA_ARGS__)

extern const char _runner_elf[], _runner_elf_end[];

static uint32_t next_object;
static bool started;
static sandbox_mailbox_t *const mailbox = (sandbox_mailbox_t *)HOST_MAILBOX_VADDR;

/* What backs each grant, to take it back on cap_drop */
static struct {
    uint32_t first;
    uint32_t count;
} grant_frames[CAPS_MAX];
/* Our copies of the frame caps of the surface and state; page tables may lie between them */
#define WINDOW_MAX_FRAMES (PAGES(GUI_WINDOW_MAX_BYTES) + 1)
#define GUI_WINDOW_MAX_BYTES 0x100000UL
static uint32_t window_objects[WINDOW_MAX_FRAMES];
static uint32_t window_count;

static bool ok(seL4_Error err, const char *what)
{
    if (err != seL4_NoError) {
        LOG_SANDBOX_ERR("%s failed with error %d\n", what, (int)err);
        return false;
    }
    return true;
}

/* Retype `n` objects of `type` into consecutive slots; returns the first, or -1 */
static int64_t make_objects(seL4_Word type, uint32_t n)
{
    if (n > MAX_OBJECTS - next_object) {
        LOG_SANDBOX_ERR("out of object slots\n");
        return -1;
    }
    uint32_t first = next_object;
    for (uint32_t done = 0; done < n;) {
        uint32_t batch = MIN(n - done, (uint32_t)RETYPE_BATCH);
        if (!ok(seL4_Untyped_Retype(CPTR(SLOT_UNTYPED), type, 0, OBJECTS, 0, 0, first + done, batch),
                "retyping the untyped")) {
            return -1;
        }
        done += batch;
    }
    next_object += n;
    return first;
}

/* Map the frame in object slot `frame` at `vaddr` of the VSpace in root slot `vspace`, making page tables */
static bool map_frame(uint32_t frame, unsigned vspace, seL4_Word vaddr, seL4_CapRights_t rights,
                      seL4_ARM_VMAttributes attrs)
{
    for (;;) {
        seL4_Error err = seL4_ARM_Page_Map(OBJ(frame), CPTR(vspace), vaddr, rights, attrs);
        if (err != seL4_FailedLookup) {
            return ok(err, "mapping a frame");
        }
        int64_t pt = make_objects(seL4_ARM_PageTableObject, 1);
        if (pt < 0 || !ok(seL4_ARM_PageTable_Map(OBJ(pt), CPTR(vspace), vaddr, seL4_ARM_Default_VMAttributes),
                          "mapping a page table")) {
            return false;
        }
    }
}

static bool map_frames(uint32_t first, uint32_t n, unsigned vspace, seL4_Word vaddr, seL4_CapRights_t rights,
                       seL4_ARM_VMAttributes attrs)
{
    for (uint32_t i = 0; i < n; i++) {
        if (!map_frame(first + i, vspace, vaddr + i * PAGE, rights, attrs)) {
            return false;
        }
    }
    return true;
}

static void unmap_frames(uint32_t first, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        seL4_ARM_Page_Unmap(OBJ(first + i));
    }
}

/* Make `n` frames and map them into our window; returns the first, or -1 */
static int64_t make_filled_frames(uint32_t n)
{
    if (n * PAGE > HOST_WINDOW_SIZE) {
        LOG_SANDBOX_ERR("region too large\n");
        return -1;
    }
    int64_t first = make_objects(seL4_ARM_SmallPageObject, n);
    if (first < 0 || !map_frames(first, n, SLOT_OWN_VSPACE, HOST_WINDOW_VADDR, seL4_ReadWrite,
                                 seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever)) {
        return -1;
    }
    return first;
}

/* Load the runner's segments into the sandbox; returns its entry point, or 0 */
static seL4_Word load_runner(void)
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
        int64_t first = make_filled_frames(n);
        if (first < 0) {
            return 0;
        }
        /* Frames come zeroed from the kernel, which covers .bss */
        memcpy((char *)HOST_WINDOW_VADDR + (ph->p_vaddr - start), _runner_elf + ph->p_offset, ph->p_filesz);
        bool exec = ph->p_flags & PF_X;
        if (exec) {
            for (uint32_t j = 0; j < n; j++) {
                seL4_ARM_Page_Clean_Data(OBJ(first + j), 0, PAGE);
            }
        }
        unmap_frames(first, n);

        seL4_CapRights_t rights = (ph->p_flags & PF_W) ? seL4_ReadWrite : seL4_CanRead;
        seL4_ARM_VMAttributes attrs = seL4_ARM_Default_VMAttributes | (exec ? 0 : seL4_ARM_ExecuteNever);
        if (!map_frames(first, n, SLOT_SANDBOX_VSPACE, start, rights, attrs)) {
            return 0;
        }
        if (exec) {
            for (uint32_t j = 0; j < n; j++) {
                seL4_ARM_Page_Unify_Instruction(OBJ(first + j), 0, PAGE);
            }
        }
    }
    return eh->e_entry;
}

/* Read `size` bytes of `path` into new frames mapped in the sandbox at `vaddr` with `rights` */
static int64_t load_file(sandbox_read_fn read, const char *path, uint64_t size, seL4_Word vaddr,
                         seL4_CapRights_t rights)
{
    uint32_t n = MAX(PAGES(size), 1);
    int64_t first = make_filled_frames(n);
    if (first < 0) {
        return -1;
    }
    int64_t got = read(path, 0, (uint8_t *)HOST_WINDOW_VADDR, size);
    unmap_frames(first, n);
    if (got != (int64_t)size) {
        LOG_SANDBOX_ERR("could not read %s\n", path);
        return -1;
    }
    if (!map_frames(first, n, SLOT_SANDBOX_VSPACE, vaddr, rights,
                    seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever)) {
        return -1;
    }
    return first;
}

/* Map copies of the frames of the region whose frame caps are in root slot `slot` */
static bool grant_region(unsigned slot, uint32_t n, seL4_Word vaddr)
{
    for (uint32_t i = 0; i < n; i++) {
        uint32_t copy = next_object++;
        if (copy >= MAX_OBJECTS || window_count >= WINDOW_MAX_FRAMES
            || !ok(seL4_CNode_Copy(OBJECTS, copy, SLOT_DEPTH, LEAF(slot), i, SLOT_DEPTH, seL4_ReadWrite),
                   "copying a frame of the window")) {
            return false;
        }
        window_objects[window_count++] = copy;
        if (!map_frame(copy, SLOT_SANDBOX_VSPACE, vaddr + i * PAGE, seL4_ReadWrite,
                       seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever)) {
            return false;
        }
    }
    return true;
}

static void revoke_window(void)
{
    for (uint32_t i = 0; i < window_count; i++) {
        seL4_CNode_Delete(OBJECTS, window_objects[i], SLOT_DEPTH);
    }
    window_count = 0;
}

bool sandbox_init(void)
{
    /* The CNode for the sandbox's objects, with a guard so that OBJ(i) names slot i */
    return ok(seL4_Untyped_Retype(CPTR(SLOT_BOOK_UNTYPED), seL4_CapTableObject, OBJECT_BITS, SELF, 0, 0,
                                  SLOT_OBJECTS_RAW, 1),
              "making the object CNode")
        && ok(seL4_CNode_Mint(SELF, SLOT_OBJECTS, SLOT_DEPTH, SELF, SLOT_OBJECTS_RAW, SLOT_DEPTH, seL4_AllRights,
                              seL4_CNode_CapData_new(0, OBJECTS_GUARD).words[0]),
              "setting the guard of the object CNode");
}

void sandbox_stop(void)
{
    if (!started) {
        return;
    }
    microkit_pd_stop(SANDBOX_CHILD_ID);
    revoke_window();
    /* Everything else was made from the Untyped */
    ok(seL4_CNode_Revoke(SELF, SLOT_UNTYPED, SLOT_DEPTH), "revoking the sandbox's untyped");
    next_object = 0;
    started = false;
}

bool sandbox_start(const char *name, const char *module_path, uint64_t module_size, caps_table_t *caps,
                   uint32_t width, uint32_t height, sandbox_read_fn read, char *error, size_t error_len)
{
    sandbox_stop();
    started = true;
    next_object = 0;
    window_count = 0;
    memset(grant_frames, 0, sizeof(grant_frames));
    snprintf(error, error_len, "could not build the sandbox");

    if (module_size == 0 || module_size > SANDBOX_MODULE_MAX) {
        snprintf(error, error_len, "module too large");
        goto fail;
    }

    seL4_Word entry = load_runner();
    if (!entry) {
        goto fail;
    }

    int64_t stack = make_objects(seL4_ARM_SmallPageObject, PAGES(SANDBOX_STACK_SIZE));
    int64_t heap = make_objects(seL4_ARM_SmallPageObject, PAGES(SANDBOX_HEAP_SIZE));
    if (stack < 0 || heap < 0
        || !map_frames(stack, PAGES(SANDBOX_STACK_SIZE), SLOT_SANDBOX_VSPACE, SANDBOX_STACK_VADDR, seL4_ReadWrite,
                       seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever)
        || !map_frames(heap, PAGES(SANDBOX_HEAP_SIZE), SLOT_SANDBOX_VSPACE, SANDBOX_HEAP_VADDR, seL4_ReadWrite,
                       seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever)) {
        goto fail;
    }

    /* WAMR's loader rewrites parts of the module in place; it is the sandbox's own copy */
    if (load_file(read, module_path, module_size, SANDBOX_MODULE_VADDR, seL4_ReadWrite) < 0) {
        snprintf(error, error_len, "could not read the module");
        goto fail;
    }

    /* The mailbox, which we map too */
    int64_t box = make_objects(seL4_ARM_SmallPageObject, 1);
    if (box < 0
        || !map_frame(box, SLOT_SANDBOX_VSPACE, SANDBOX_MAILBOX_VADDR, seL4_ReadWrite,
                      seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever)) {
        goto fail;
    }
    uint32_t box_copy = next_object++;
    if (!ok(seL4_CNode_Copy(OBJECTS, box_copy, SLOT_DEPTH, OBJECTS, box, SLOT_DEPTH, seL4_ReadWrite),
            "copying the mailbox")
        || !map_frame(box_copy, SLOT_OWN_VSPACE, HOST_MAILBOX_VADDR, seL4_ReadWrite,
                      seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever)) {
        goto fail;
    }

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
            int64_t first = load_file(read, cap->path, size, vaddr, seL4_CanRead);
            if (first < 0) {
                snprintf(error, error_len, "could not read %s", cap->path);
                goto fail;
            }
            sg->vaddr = vaddr;
            sg->size = size;
            grant_frames[i].first = first;
            grant_frames[i].count = MAX(PAGES(size), 1);
            files++;
        } else if (cap->type == CAP_WINDOW) {
            g.width = width;
            g.height = height;
            if (!grant_region(SLOT_SURFACE, PAGES((uint64_t)width * height * 4), SANDBOX_SURFACE_VADDR)
                || !grant_region(SLOT_STATE, 1, SANDBOX_STATE_VADDR)) {
                goto fail;
            }
        }
    }

    int64_t grants_frame = make_filled_frames(1);
    if (grants_frame < 0) {
        goto fail;
    }
    memcpy((void *)HOST_WINDOW_VADDR, &g, sizeof(g));
    unmap_frames(grants_frame, 1);
    if (!map_frame(grants_frame, SLOT_SANDBOX_VSPACE, SANDBOX_GRANTS_VADDR, seL4_CanRead,
                   seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever)) {
        goto fail;
    }

    printf("WASM HOST|INFO: sandbox for %s: %u objects made, %d file(s) and %s mapped\n", name, next_object, files,
           window_count ? "the window" : "no window");
    microkit_pd_restart(SANDBOX_CHILD_ID, entry);
    return true;

fail:
    sandbox_stop();
    return false;
}

void sandbox_revoke_grant(int handle, cap_type_t type)
{
    if (!started || handle < 0 || handle >= CAPS_MAX) {
        return;
    }
    if (type == CAP_WINDOW) {
        revoke_window();
    } else if (type == CAP_FILE) {
        unmap_frames(grant_frames[handle].first, grant_frames[handle].count);
        grant_frames[handle].count = 0;
    }
}

static bool post(sandbox_msg_t msg)
{
    if (!started) {
        return false;
    }
    uint32_t tail = mailbox->msg_tail;
    uint32_t head = __atomic_load_n(&mailbox->msg_head, __ATOMIC_ACQUIRE);
    /* The runner writes msg_head, so it may be anything; a full or corrupt queue just drops */
    if (tail - head >= SANDBOX_MSGS) {
        return false;
    }
    mailbox->msgs[tail % SANDBOX_MSGS] = msg;
    __atomic_store_n(&mailbox->msg_tail, tail + 1, __ATOMIC_RELEASE);
    return true;
}

bool sandbox_post_event(gui_event_t ev)
{
    return post((sandbox_msg_t) { .type = SANDBOX_MSG_EVENT, .ev = ev });
}

bool sandbox_post_tick(uint32_t time_ms)
{
    return post((sandbox_msg_t) { .type = SANDBOX_MSG_TICK, .time_ms = time_ms });
}

void sandbox_kick(void)
{
    if (started) {
        microkit_notify(SANDBOX_HOST_CH);
    }
}

bool sandbox_next_request(sandbox_req_t *req)
{
    if (!started) {
        return false;
    }
    uint32_t head = mailbox->req_head;
    uint32_t tail = __atomic_load_n(&mailbox->req_tail, __ATOMIC_ACQUIRE);
    if (tail == head) {
        return false;
    }
    if (tail - head > SANDBOX_REQS) {
        /* The runner wrote a nonsensical index: drop everything */
        __atomic_store_n(&mailbox->req_head, tail, __ATOMIC_RELEASE);
        return false;
    }
    *req = mailbox->reqs[head % SANDBOX_REQS];
    __atomic_store_n(&mailbox->req_head, head + 1, __ATOMIC_RELEASE);

    /* Keep only printable text */
    req->text[SANDBOX_TEXT_MAX - 1] = '\0';
    for (char *c = req->text; *c; c++) {
        if (*c < ' ' || *c > '~') {
            *c = '?';
        }
    }
    return true;
}
