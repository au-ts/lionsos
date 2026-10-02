#include <stdint.h>
#include <stdbool.h>

#define MAILBOX_REGS_VADDR      0x30000000UL
#define MAILBOX_REGS_OFFSET     0x880UL
#define MAILBOX_BUFFER_VADDR    0x30001000UL
#define MAILBOX_BUFFER_PADDR    0x01000000UL
#define MAILBOX_BUFFER_SIZE     0x1000UL

#define MBOX_BUS_UNCACHED   0xc0000000U

// tags to get stuff from the VideoCore firmware
#define MBOX_TAG_GET_FIRMWARE_REVISION  0x00000001U
#define MBOX_TAG_GET_BOARD_REVISION     0x00010002U
#define MBOX_TAG_GET_TEMPERATURE        0x00030006U
#define MBOX_TAG_GET_THROTTLED          0x00030046U
#define MBOX_TAG_GET_CLOCK_RATE         0x00030002U
#define MBOX_TAG_GET_MAX_CLOCK_RATE     0x00030004U
#define MBOX_TAG_GET_MIN_CLOCK_RATE     0x00030007U
#define MBOX_TAG_GET_CLOCK_RATE_MEASURED 0x00030047U
#define MBOX_TAG_SET_CLOCK_RATE         0x00038002U

#define MBOX_CODE_REQUEST       0x00000000U
#define MBOX_CODE_SUCCESS       0x80000000U
#define MBOX_TAG_END            0x00000000U
#define MBOX_TAG_RESPONSE       0x80000000U

#define MBOX_MAX_VALUE_WORDS 8

#define MBOX_SET_CLOCK_SKIP_TURBO       1U

#define MAILBOX_CLOCK_ARM       0x3

#define MBOX0_READ      0x00
#define MBOX0_STATUS    0x18
#define MBOX1_WRITE     0x20
#define MBOX1_STATUS    0x38

#define MBOX_STATUS_FULL    0x80000000U
#define MBOX_STATUS_EMPTY   0x40000000U

#define MBOX_CHANNEL_PROPERTY   0x8
#define MBOX_CHANNEL_MASK       0xfU

#define MBOX_STATUS_FULL    0x80000000U
#define MBOX_STATUS_EMPTY   0x40000000U

#define MBOX_POLL_LIMIT     (1U << 22)

bool mailbox_init(void);
bool mailbox_get_clock_rate(uint32_t clock_id, uint32_t *rate_hz);
bool mailbox_set_clock_rate(uint32_t clock_id, uint32_t rate_hz, uint32_t *actual_hz);
bool mailbox_pin_arm_clock(uint32_t target_hz);