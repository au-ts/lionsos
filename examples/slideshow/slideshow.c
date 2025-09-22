/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include <string.h>
#include <stdio.h>
#include <libmicrokitco.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/timer/config.h>
#include <sddf/util/printf.h>
#include <lions/fs/config.h>
#include <lions/fs/protocol.h>

#include <hdmi/hdmi_data.h>
#include <hdmi/vic_table.h>
#include <api/frame_buffer.h>

#include "fs_blocking_calls.h"
#include "fs_client_helpers.h"

#define SLIDESHOW_FOLDER_PATH "/"

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

#define WORKER_STACK_SIZE 0x10000
static char worker_stack[WORKER_STACK_SIZE];
static co_control_t co_controller_mem;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

serial_queue_handle_t serial_rx_queue_handle;
serial_queue_handle_t serial_tx_queue_handle;

#define DCSS_INIT_CH 42
#define DCSS_DRAW_CH 43
volatile struct hdmi_data *shared_hdmi_config = (struct hdmi_data *) 0x60000000;

#define MAX_NUM_SLIDES 100
uint64_t slide_dir_locations[MAX_NUM_SLIDES];
uint64_t num_slides = 0;

void read_slide(char *filename) {
    char path[FS_MAX_PATH_LENGTH];
    strcpy(path, SLIDESHOW_FOLDER_PATH);
    strcat(path, filename);

    uint64_t fd = fs_file_open_blocking(path, strlen(path), FS_OPEN_FLAGS_READ_ONLY);
}

void framebuffer_draw_test_pattern(void) {
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

    microkit_ppcall(DCSS_INIT_CH, seL4_MessageInfo_new(0, 0, 0, 0));

    // wait for DCSS to be ready
    microkit_cothread_wait_on_channel(DCSS_DRAW_CH);
}

void slideshow_worker(void) {
    sddf_printf("slideshow: slideshow_worker(): initialising video...\n");
    video_init();
    sddf_printf("slideshow: slideshow_worker(): video initialised!\n");

    sddf_printf("slideshow: slideshow_worker(): mounting filesystem...");
    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t){ .type = FS_CMD_INITIALISE });
    if (err || completion.status != FS_STATUS_SUCCESS) {
        sddf_printf("FAIL\n");
        return;
    } else {
        sddf_printf("OK\n");
    }

    sddf_printf("slideshow: slideshow_worker(): opening slides folder.\n");
    uint64_t dir_fd = fs_dir_open_blocking(SLIDESHOW_FOLDER_PATH, strlen(SLIDESHOW_FOLDER_PATH));

    sddf_printf("slideshow: slideshow_worker(): enumerating slides (check order!!):\n");
    char file_name[FS_MAX_NAME_LENGTH];
    while (fs_dir_read_blocking(dir_fd, file_name) > 0) {
        uint64_t dir_pos = fs_dir_tell_blocking(dir_fd);
        sddf_printf("-> Directory position: %lu, filename: '%s'\n", dir_pos, file_name);
        slide_dir_locations[num_slides] = dir_pos;
        num_slides += 1;
    }
    sddf_printf("slideshow: slideshow_worker(): found %lu slides!\n", num_slides);

    sddf_printf("slideshow: slideshow_worker(): reading first slide into memory.\n");
    int cur_slide = 0;
    fs_dir_seek_blocking(dir_fd, slide_dir_locations[cur_slide]);

    sddf_printf("slideshow: slideshow_worker(): READY TO RECEIVE COMMANDS.\n");
    sddf_printf("Press 'a' to go backward, 'd' to go forward. Make sure CAPS LOCK is off.\n");
    while (true) {
        microkit_cothread_wait_on_channel(serial_config.rx.id);

        char c;
        if (serial_dequeue(&serial_rx_queue_handle, &c) != 0) {
            continue;
        }

        int new_slide;
        if (c == 'a') {
            new_slide = (cur_slide - 1) % num_slides;
            sddf_printf("Going backward from slide #%d to #%d.\n", cur_slide, new_slide);
        } else if (c == 'd') {
            new_slide = (cur_slide + 1) % num_slides;
            sddf_printf("Going forward from slide #%d to #%d.\n", cur_slide, new_slide);
        }
        cur_slide = new_slide;

        

        if (cur_slide % 2) {
            framebuffer_draw_test_pattern();
            framebuffer_kick();
        } else {
            clear_current_frame_buffer(shared_hdmi_config);
            framebuffer_kick();
        }
    }
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

    if (microkit_cothread_spawn(slideshow_worker, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        sddf_printf("slideshow: init(): ERROR: cannot spawn the slideshow worker coroutine.\n");
        return;
    }

    sddf_printf("slideshow: init(): initialisation completed, jumping to worker coroutine.\n");
    microkit_cothread_yield();
}

void notified(microkit_channel ch) {
    if (ch == fs_config.server.id) {
        fs_process_completions();
    }
    microkit_cothread_recv_ntfn(ch);
}
