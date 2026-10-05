#include "mailbox.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

static volatile uint32_t *mbox_regs;
static volatile uint32_t *mbox_buffer;
static uint32_t mbox_bus_addr;
static bool mbox_ready;

static inline void dsb(void)
{
    asm volatile("dsb sy" ::: "memory");
}

static inline uint32_t reg_read(unsigned offset)
{
    return mbox_regs[offset / 4];
}

static inline void reg_write(unsigned offset, uint32_t value)
{
    mbox_regs[offset / 4] = value;
}

static bool mbox_exchange(uint32_t bus_addr)
{
    uint32_t message = (bus_addr & ~MBOX_CHANNEL_MASK) | MBOX_CHANNEL_PROPERTY;
    dsb();
    unsigned spins = MBOX_POLL_LIMIT;

    while (reg_read(MBOX1_STATUS) & MBOX_STATUS_FULL) {
        if (--spins == 0) {
            return false;
        }
    }
    reg_write(MBOX1_WRITE, message);

    for (;;) {
        if (reg_read(MBOX0_STATUS) & MBOX_STATUS_EMPTY) {
            if (--spins == 0) {
                return false;
            }
            continue;
        }
        if (reg_read(MBOX0_READ) == message) {
            break;
        }
        if (--spins == 0) {
            return false;
        }
    }

    dsb();
    return true;
}

static bool mbox_property(uint32_t tag, const uint32_t *req, unsigned req_words,
                          uint32_t *resp, unsigned resp_words)
{
    if (!mbox_ready) {
        return false;
    }

    unsigned value_words = req_words > resp_words ? req_words : resp_words;
    if (value_words > MBOX_MAX_VALUE_WORDS) {
        return false;
    }

    unsigned words = 2 + 3 + value_words + 1;
    unsigned size = ((words * 4) + 15) & ~15U;

    mbox_buffer[0] = size;
    mbox_buffer[1] = MBOX_CODE_REQUEST;
    mbox_buffer[2] = tag;
    mbox_buffer[3] = value_words * 4;
    mbox_buffer[4] = MBOX_CODE_REQUEST;
    for (unsigned i = 0; i < value_words; i++) {
        mbox_buffer[5 + i] = i < req_words ? req[i] : 0;
    }
    mbox_buffer[5 + value_words] = MBOX_TAG_END;

    for (unsigned i = 6 + value_words; i < size / 4; i++) {
        mbox_buffer[i] = 0;
    }

    if (!mbox_exchange(mbox_bus_addr)) {
        return false;
    }

    if (mbox_buffer[1] != MBOX_CODE_SUCCESS) {
        return false;
    }

    if (!(mbox_buffer[4] & MBOX_TAG_RESPONSE)) {
        return false;
    }
    if ((mbox_buffer[4] & ~MBOX_TAG_RESPONSE) < resp_words * 4) {
        return false;
    }

    for (unsigned i = 0; i < resp_words; i++) {
        resp[i] = mbox_buffer[5 + i];
    }
    return true;
}

bool mailbox_get_firmware_revision(uint32_t *revision)
{
    return mbox_property(MBOX_TAG_GET_FIRMWARE_REVISION, NULL, 0, revision, 1);
}

bool mailbox_init(void)
{
    if (mbox_ready) {
        return true;
    }

    mbox_regs = (volatile uint32_t *)(MAILBOX_REGS_VADDR + MAILBOX_REGS_OFFSET);
    mbox_buffer = (volatile uint32_t *)MAILBOX_BUFFER_VADDR;

    mbox_ready = true;

    static const uint32_t aliases[] = {
        MAILBOX_BUFFER_PADDR | MBOX_BUS_UNCACHED,
        MAILBOX_BUFFER_PADDR,
    };
    // return true;
    for (unsigned i = 0; i < sizeof(aliases) / sizeof(aliases[0]); i++) {
        uint32_t revision;

        mbox_bus_addr = aliases[i];
        if (mailbox_get_firmware_revision(&revision)) {
            return true;
        }
    }

    mbox_ready = false;
    return false;
}

bool mailbox_set_clock_rate(uint32_t clock_id, uint32_t rate_hz, uint32_t *actual_hz)
{
    uint32_t req[3] = { clock_id, rate_hz, MBOX_SET_CLOCK_SKIP_TURBO };
    uint32_t resp[2] = { 0, 0 };

    if (!mbox_property(MBOX_TAG_SET_CLOCK_RATE, req, 3, resp, 2)) {
        return false;
    }
    if (resp[0] != clock_id) {
        return false;
    }

    if (actual_hz != NULL) {
        *actual_hz = resp[1];
    }
    return true;
}

static bool clock_query(uint32_t tag, uint32_t clock_id, uint32_t *rate_hz)
{
    uint32_t req[1] = { clock_id };
    uint32_t resp[2] = { 0, 0 };

    if (!mbox_property(tag, req, 1, resp, 2)) {
        return false;
    }
    if (resp[0] != clock_id) {
        return false;
    }

    *rate_hz = resp[1];
    return true;
}

bool mailbox_pin_arm_clock(uint32_t target_hz)
{
    printf("\nRaspberry Pi 4B pinning clock rate at %lu\n", target_hz);

    if (!mailbox_init()) {
        printf("the VideoCore firmware does not answer\n");
        return false;
    }

    uint32_t before = 0;
    if (clock_query(MBOX_TAG_GET_CLOCK_RATE, MAILBOX_CLOCK_ARM, &before)) {
        printf("firmware left the cores at %u Hz\n", before);
    }

    uint32_t actual = 0;
    if (!mailbox_set_clock_rate(MAILBOX_CLOCK_ARM, target_hz, &actual)) {
        printf("the VideoCore firmware refused to set the ARM clock rate\n");
        return false;
    }

    if (actual != target_hz) {
        printf("the VideoCore firmware settled on %u Hz instead of the requested %u Hz\n", actual, target_hz);
        return false;
    }

    printf("CPU successfully pinned at %u Hz\n", target_hz);
    return true;
}