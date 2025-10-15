/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include "serialkeyboard.h"
#include "usb_hid_keys.h"
#include "doom.h"

#define DOOM_FILE_PATH "/doom1.wad"

#ifdef SERIAL_KD_NONSTATIC
#error "DOOM expects serial keyboard library in static mode!"
#endif

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

static char worker_stack[WORKER_STACK_SIZE];
static co_control_t co_controller_mem;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

serial_queue_handle_t serial_rx_queue_handle;
serial_queue_handle_t serial_tx_queue_handle;

volatile struct hdmi_data *shared_hdmi_config = (struct hdmi_data *) 0x60000000;

uint8_t cached_framebuffer[FRAME_SZ_BYTES];

// Keyboard event queue for DOOM
#define KEY_QUEUE_SZ 32
uint8_t kq[KEY_QUEUE_SZ];
unsigned kq_tail = 0;
unsigned kq_head = 0;

static void addKeyToQueue(int pressed, unsigned int keyCode)
{
    uint8_t key = convertToDoomKey(keyCode);

    uint8_t keyData = (pressed << 8) | key;

    kq[kq_tail] = keyData;
    kq_tail++;
    kq_tail %= KEY_QUEUE_SZ;
}

static bool keyQueueFull(void)
{
    return (kq_tail - kq_head == KEY_QUEUE_SZ);
}

void framebuffer_draw_test_pattern(void)
{
    uint8_t* frame_buffer_addr = get_active_frame_buffer_uint8();

    int height = shared_hdmi_config->v_active;
    int width = shared_hdmi_config->h_active;
    int first_quarter = width * 0.25;
    int second_quarter = width * 0.5;
    int third_quarter = width * 0.75;
    int alpha = 0;

    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {

            // reset alpha for each colour bar
            if (j % first_quarter == 0) {
                alpha = 0;
            }

            if (j < first_quarter)
            {
                *(frame_buffer_addr++) = 0xff;
                *(frame_buffer_addr++) = 0x00;
                *(frame_buffer_addr++) = 0x00;
                *(frame_buffer_addr++) = alpha;
            }
            else if (j < second_quarter)
            {
                *(frame_buffer_addr++) = 0x00;
                *(frame_buffer_addr++) = 0xff;
                *(frame_buffer_addr++) = 0x00;
                *(frame_buffer_addr++) = alpha;
            }
            else if (j < third_quarter)
            {
                *(frame_buffer_addr++) = 0x00;
                *(frame_buffer_addr++) = 0x00;
                *(frame_buffer_addr++) = 0xff;
                *(frame_buffer_addr++) = alpha;
            }
            else {
                *(frame_buffer_addr++) = 0xff;
                *(frame_buffer_addr++) = 0xff;
                *(frame_buffer_addr++) = 0xff;
                *(frame_buffer_addr++) = alpha;
            }

            if (j %3 == 0) {
                alpha++;
            }
        }
    }
}

void framebuffer_kick(void) {
    microkit_notify(DCSS_DRAW_CH);
    microkit_cothread_wait_on_channel(DCSS_DRAW_CH);
}

void video_init(void) {
    // 1920 x 1080, 60hz, progressive scan
    int vic_mode = VIC_MODE_16_60Hz;

    shared_hdmi_config->h_front_porch = vic_table[vic_mode][FRONT_PORCH];
    shared_hdmi_config->h_back_porch= vic_table[vic_mode][BACK_PORCH];
    shared_hdmi_config->hsync = vic_table[vic_mode][HSYNC];
    shared_hdmi_config->v_front_porch = vic_table[vic_mode][TYPE_EOF];
    shared_hdmi_config->v_back_porch = vic_table[vic_mode][SOF];
    shared_hdmi_config->vsync= vic_table[vic_mode][VSYNC];
    shared_hdmi_config->h_active = vic_table[vic_mode][H_ACTIVE];
    shared_hdmi_config->v_active = vic_table[vic_mode][V_ACTIVE];
    shared_hdmi_config->hsync_pol = vic_table[vic_mode][HSYNC_POL];
    shared_hdmi_config->vsync_pol = vic_table[vic_mode][VSYNC_POL];
    shared_hdmi_config->pixel_frequency_khz = vic_table[vic_mode][PIXEL_FREQ_KHZ];
    shared_hdmi_config->h_blank = vic_table[vic_mode][H_BLANK];
    shared_hdmi_config->h_total = vic_table[vic_mode][H_TOTAL];
    shared_hdmi_config->vic_r3 = vic_table[vic_mode][VIC_R3_0];
    shared_hdmi_config->vic_pr = vic_table[vic_mode][VIC_PR];
    shared_hdmi_config->v_total = vic_table[vic_mode][V_TOTAL];
    shared_hdmi_config->rgb_format = RGBA;
    shared_hdmi_config->alpha_enable = ALPHA_OFF;
    shared_hdmi_config->mode = MOVING_IMAGE;
    shared_hdmi_config->ms_delay = NO_DELAY;

    framebuffer_draw_test_pattern();

    // initialise video hardware (Display Controller Subsystem + HDMI TX)
    microkit_ppcall(DCSS_INIT_CH, seL4_MessageInfo_new(0, 0, 0, 0));

    // wait for DCSS to be ready
    microkit_cothread_wait_on_channel(DCSS_DRAW_CH);
}


void doom_main(void) {

    /*sddf_printf("doom: doom_main(): mounting filesystem...");*/
    /*fs_cmpl_t completion;*/
    /*int err = fs_command_blocking(&completion, (fs_cmd_t){ .type = FS_CMD_INITIALISE });*/
    /*if (err || completion.status != FS_STATUS_SUCCESS) {*/
    /*    sddf_printf("FAIL\n");*/
    /*    return;*/
    /*} else {*/
    /*    sddf_printf("OK\n");*/
    /*}*/
    /**/
    /*sddf_printf("doom: doom_main(): opening doom1.wad.\n");*/
    /*uint64_t wad_fd = fs_file_open_blocking(DOOM_FILE_PATH, strlen(doom_FILE_PATH), FS_OPEN_FLAGS_READ_ONLY);*/
    /*uint64_t file_size = fs_file_size_blocking(wad_fd);*/
    /*assert(file_size % FRAME_SZ_BYTES == 0);*/
    /*sddf_printf("doom: doom_main(): loaded %s. %lu bytes.\n", DOOM_FILE_PATH, file_size);*/

    // Simulate commandline arguments
    int argc = 3;
    const char *argv[3] = {"doom", "-iwad", "./doom1.wad"};

    // Prepare
    doomgeneric_Create(argc, argv);

    while (true) {
        char c;
        while (serial_dequeue(&serial_rx_queue_handle, &c) == 0 &&
            !keyQueueFull()) {
            hid_key_t key = serialkb_input_serial_char(c);
            int pressed = HID_KEY_PRESS(key);
            addKeyToQueue(pressed, key);
        }

        // Advance doom engine
        doomgeneric_Tick();
        sddf_printf("doom: doom_main(): loop completed\n");
    }
}

/**
  * genericdoom init
  * Set up graphics.
  */
void DG_Init() {
    video_init();
}

/**
 * genericdoom frame update
 * Kick frame buffer to update from doom frame buffer
 */
void DG_DrawFrame() {
    uint8_t* frame_buffer_addr = get_active_frame_buffer_uint8();
    memcpy((char *)DG_ScreenBuffer, frame_buffer_addr, DOOMGENERIC_RESX*DOOMGENERIC_RESY*4);
    framebuffer_kick();
}

void DG_SleepMs(uint32_t ms) {
    // Convert to ns
    sddf_timer_set_timeout(timer_config.driver_id, (uint64_t) ms * 1000);
    microkit_cothread_wait_on_channel(timer_config.driver_id);
}

uint32_t DG_GetTicksMs() {
    // Return current time in microseconds
    return (uint32_t) (sddf_timer_time_now(timer_config.driver_id) / 1000);
}

int DG_GetKey(int *pressed, unsigned char *doomKey) {
    // Return keys from the key queue until none are left.
    if (kq_head == kq_tail) return 0;
    uint8_t keyData = kq[kq_head];
    kq_head++;
    kq_head %= KEY_QUEUE_SZ;

    *pressed = keyData >> 8;
    *doomKey = keyData & 0xFF;

    return 1;
}

void DG_SetWindowTitle(const char * title) {
    // Do nothing
    (void) title;
    return;
}

void init(void) {
    assert(serial_config_check_magic(&serial_config));
    assert(timer_config_check_magic(&timer_config));
    assert(fs_config_check_magic(&fs_config));

    serial_queue_init(&serial_rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size, serial_config.rx.data.vaddr);
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size, serial_config.tx.data.vaddr);

    fs_command_queue = fs_config.server.command_queue.vaddr;
    fs_completion_queue = fs_config.server.completion_queue.vaddr;
    fs_share = fs_config.server.share.vaddr;

    stack_ptrs_arg_array_t costacks = { (uintptr_t) worker_stack };
    microkit_cothread_init(&co_controller_mem, WORKER_STACK_SIZE, costacks);

    if (microkit_cothread_spawn(doom_main, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        sddf_printf("doom: init(): ERROR: cannot spawn the doom worker coroutine.\n");
        return;
    }

    sddf_printf("doom: init(): initialisation completed, jumping to worker coroutine.\n");
    microkit_cothread_yield();
}

void notified(microkit_channel ch) {
    /*if (ch == fs_config.server.id) {*/
    /*    fs_process_completions();*/
    /*}*/
    microkit_cothread_recv_ntfn(ch);
}
