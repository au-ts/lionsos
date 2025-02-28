/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <linux/limits.h>

#include <lions/fs/protocol.h>

#include "vmfs_shared.h"
#include "log.h"
#include "op.h"
#include "util.h"

#include <liburing.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define ARGC_REQURED 3

#define UIO_COMPAT_MAX_LEN 64

#define UIO_PATH "/sys/class/uio"
#define UIO_PATH_MAX 128
char uio_dev_path[UIO_PATH_MAX];

/* Event queue for polling */
#define MAX_EVENTS 16 // Arbitrary length
struct epoll_event events[MAX_EVENTS];

char blk_device[PATH_MAX];
int blk_device_len;
char mnt_point[PATH_MAX];
int mnt_point_len;

/* Shared config data between VMM and guest (where this is running) */
vmm_to_guest_conf_data_t *conf;

/* Shared queues and data to native client via UIO */
struct fs_queue *cmd_queue;
struct fs_queue *comp_queue;
char *fs_data;

/* Our copy of the client data share, it is unsafe to give their address directly
   to kernel as the underlying block device may DMA. Another reason is that UIO map
   the memory as device memory, which cause weird behaviours with syscalls. */
char *our_data_region;

/* Guest -> VMM fault address via UIO, unmapped in SDF */
char *vmm_notify_fault;

struct io_uring ring;

int create_epoll(void)
{
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("create_epoll(): epoll_create1()");
        LOG_FS_ERR("can't create the epoll fd.\n");
        exit(EXIT_FAILURE);
    }
    return epoll_fd;
}

void bind_fd_to_epoll(int fd, int epollfd)
{
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = fd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event) == -1) {
        perror("bind_fd_to_epoll(): epoll_ctl()");
        LOG_FS_ERR("can't register fd %d to epoll fd %d.\n", fd, epollfd);
        exit(EXIT_FAILURE);
    }
}

// Find a UIO device path by its name
bool find_uio_by_name(const char *target_name, char *dev_path) {
    DIR *dir;
    struct dirent *entry;
    char name[UIO_COMPAT_MAX_LEN];
    char name_path[UIO_PATH_MAX];
    int fd;
    ssize_t bytes_read;
    bool found = false;

    dir = opendir(UIO_PATH);
    if (!dir) {
        perror("Failed to open UIO directory");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (strncmp(entry->d_name, "uio", 3) == 0) {
            snprintf(name_path, UIO_PATH_MAX, "%s/%s/device/of_node/compatible", UIO_PATH, entry->d_name);
            
            // Read the compat string
            fd = open(name_path, O_RDONLY);
            if (fd < 0) {
                continue;
            }
            bytes_read = read(fd, name, UIO_COMPAT_MAX_LEN - 1);
            close(fd);
            
            if (bytes_read > 0) {
                // Null-terminate the string (removing any newline)
                if (name[bytes_read-1] == '\n')
                    bytes_read--;
                name[bytes_read] = '\0';

                if (bytes_read <= strlen("generic-uio") + 2) { // plus the string's null term then the next first character
                    // Should never get here unless the sdfgen tool is broken.
                    LOG_FS_ERR("found a compat string without name!");
                }

                // Since the compat string is something like `generic-uio\0<name>`
                // Find the first null term.
                char *node_name = memchr(name, 0, bytes_read) + 1;

                // Check if this is the device we're looking for
                if (strcmp(node_name, target_name) == 0) {
                    snprintf(dev_path, UIO_PATH_MAX, "/dev/%s", entry->d_name);
                    found = true;
                    break;
                }
            }
        }
    }

    closedir(dir);
    return found;
}

int open_uio(const char *abs_path)
{
    int fd = open(abs_path, O_RDWR);
    if (fd == -1) {
        perror("open_uio(): open()");
        LOG_FS_ERR("can't open uio @ %s.\n", abs_path);
        exit(EXIT_FAILURE);
    }
    return fd;
}

char *map_uio(uint64_t length, int uiofd)
{
    void *base = (char *) mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, uiofd, 0);
    if (base == MAP_FAILED) {
        perror("map_uio(): mmap()");
        LOG_FS_ERR("can't mmap uio fd %d\n", uiofd);
        exit(EXIT_FAILURE);
    }
    return (char *) base;
}

void uio_interrupt_ack(int uiofd)
{
    uint32_t enable = 1;
    if (write(uiofd, &enable, sizeof(uint32_t)) != sizeof(uint32_t)) {
        perror("uio_interrupt_ack(): write()");
        LOG_FS_ERR("Failed to write enable/ack interrupts on uio fd %d\n", uiofd);
        exit(EXIT_FAILURE);
    }
}

void bring_up_io_uring(void) {
    /* An optimisation hint to Linux as we only have one userland thread submitting jobs. */
    unsigned int flags = IORING_SETUP_SINGLE_ISSUER;

    /* More optimisation: don't interrupt submission processing when a completion is generated. */
    flags |= IORING_SETUP_COOP_TASKRUN;

    /* I believe there are more useful flags to us: https://man7.org/linux/man-pages/man2/io_uring_setup.2.html */
    int err = io_uring_queue_init(FS_QUEUE_CAPACITY, &ring, flags);
    if (err) {
        perror("bring_up_io_uring(): io_uring_queue_init(): ");
        exit(EXIT_FAILURE);
    }

    /* This ring will last for the lifetime of this program so there isn't ever a need to tear it down. */
}

void process_fs_commands(void)
{
    uint64_t command_count = fs_queue_length_consumer(cmd_queue);
    uint64_t completion_space = FS_QUEUE_CAPACITY - fs_queue_length_producer(comp_queue);
    /* Don't dequeue a command if we have no space to enqueue its completion */
    uint64_t to_consume = MIN(command_count, completion_space);

    /* Number of commands that completed */
    uint64_t comp_idx = 0;

    /* Enqueue all the commands to io_uring */
    for (uint64_t i = 0; i < to_consume; i++) {
        fs_cmd_t cmd = fs_queue_idx_filled(cmd_queue, i)->cmd;
        if (cmd.type >= FS_NUM_COMMANDS) {
            fs_queue_enqueue_reply((fs_cmpl_t){ .id = cmd.id, .status = FS_STATUS_INVALID_COMMAND, .data = {0} }, &comp_idx);
            comp_idx += 1;
        } else {
            cmd_handler[cmd.type](cmd, &comp_idx);
        }
    }

    fs_queue_publish_consumption(cmd_queue, to_consume);

    flush_and_wait_io_uring_sqes(&ring, &comp_idx);

    /* Finally annouce the number of completions we produced. These are left to last minute as
       ordered writes are expensive. */
    assert(comp_idx == to_consume);
    fs_queue_publish_production(comp_queue, comp_idx);
}

void notify_vmm(void) {
    char *fault_addr = (char *) vmm_notify_fault;
    *fault_addr = (char) 0;
}

int main(int argc, char **argv)
{
    if (argc != ARGC_REQURED) {
        LOG_FS_ERR("usage: ./uio_fs_driver <blk_device> <mount_point>");
        exit(EXIT_FAILURE);
    } else {
        strncpy(blk_device, argv[1], PATH_MAX);
        strncpy(mnt_point, argv[2], PATH_MAX);
        blk_device_len = strnlen(blk_device, PATH_MAX);
        mnt_point_len = strnlen(mnt_point, PATH_MAX);

        if (blk_device_len > PATH_MAX) {
            LOG_FS_ERR("usage: ./uio_fs_driver <blk_device> <mount_point>");
            LOG_FS_ERR("<blk_device> cannot be more than PATH_MAX, which is %u\n", PATH_MAX);
        }
        if (mnt_point_len > PATH_MAX) {
            LOG_FS_ERR("usage: ./uio_fs_driver <blk_device> <mount_point>");
            LOG_FS_ERR("<mount_point> cannot be more than PATH_MAX, which is %u\n", PATH_MAX);
        }
    }

    LOG_FS("*** Starting up\n");
    LOG_FS("Block device: %s\n", blk_device);
    LOG_FS("Mount point: %s\n", mnt_point);

    LOG_FS("*** Setting up shared configuration data via UIO\n");
    if (!find_uio_by_name(UIO_DEV_NAME_FS_VM_CONF, uio_dev_path)) {
        LOG_FS_ERR("Can't find UIO device %s\n", UIO_DEV_NAME_FS_VM_CONF);
        return EXIT_FAILURE;
    }
    LOG_FS("Found dev @ %s\n", uio_dev_path);
    int conf_uio_fd = open_uio(uio_dev_path);
    conf = (vmm_to_guest_conf_data_t *) map_uio(UIO_PATH_FS_VM_CONFIG_SZ, conf_uio_fd);

    our_data_region = malloc(sizeof(char) * conf->fs_data_share_region_size);
    if (!our_data_region) {
        LOG_FS_ERR("out of memory creating data region copy.");
        exit(EXIT_FAILURE);
    }

    LOG_FS("*** Setting up command queue via UIO\n");
    if (!find_uio_by_name(UIO_DEV_NAME_FS_CMD, uio_dev_path)) {
        LOG_FS_ERR("Can't find UIO device %s\n", UIO_DEV_NAME_FS_CMD);
        return EXIT_FAILURE;
    }
    LOG_FS("Found dev @ %s\n", uio_dev_path);
    int cmd_uio_fd = open_uio(uio_dev_path);
    cmd_queue = (struct fs_queue *) map_uio(conf->fs_cmd_queue_region_size, cmd_uio_fd);

    LOG_FS("*** Setting up completion queue via UIO\n");
    if (!find_uio_by_name(UIO_DEV_NAME_FS_COMP, uio_dev_path)) {
        LOG_FS_ERR("Can't find UIO device %s\n", UIO_DEV_NAME_FS_COMP);
        return EXIT_FAILURE;
    }
    LOG_FS("Found dev @ %s\n", uio_dev_path);
    int comp_uio_fd = open_uio(uio_dev_path);
    comp_queue = (struct fs_queue *) map_uio(conf->fs_comp_queue_region_size, comp_uio_fd);

    LOG_FS("*** Setting up FS data region via UIO\n");
    if (!find_uio_by_name(UIO_DEV_NAME_FS_DATA, uio_dev_path)) {
        LOG_FS_ERR("Can't find UIO device %s\n", UIO_DEV_NAME_FS_DATA);
        return EXIT_FAILURE;
    }
    LOG_FS("Found dev @ %s\n", uio_dev_path);
    int fs_data_uio_fd = open_uio(uio_dev_path);
    fs_data = map_uio(conf->fs_data_share_region_size, fs_data_uio_fd);

    LOG_FS("*** Setting up fault region via UIO\n");
    // For Guest -> VMM notifications
    if (!find_uio_by_name(UIO_DEV_NAME_FS_FAULT, uio_dev_path)) {
        LOG_FS_ERR("Can't find UIO device %s\n", UIO_DEV_NAME_FS_FAULT);
        return EXIT_FAILURE;
    }
    LOG_FS("Found dev @ %s\n", uio_dev_path);
    int fault_uio_fd = open_uio(uio_dev_path);
    vmm_notify_fault = map_uio(conf->fs_vm_to_vmm_fault_reg_size, fault_uio_fd);

    LOG_FS("*** Enabling UIO interrupt on command queue\n");
    uio_interrupt_ack(cmd_uio_fd);

    LOG_FS("*** Creating epoll object\n");
    int epoll_fd = create_epoll();

    LOG_FS("*** Binding command queue IRQ to epoll\n");
    bind_fd_to_epoll(cmd_uio_fd, epoll_fd);

    LOG_FS("*** Initialising liburing for io_uring\n");
    bring_up_io_uring();

    LOG_FS("*** Consuming requests already in command queue\n");
    // Because any native FS clients would've finished initialising way before our Linux kernel get to
    // userland.
    process_fs_commands();

    LOG_FS("*** All initialisation successful!\n");
    LOG_FS("*** You won't see any output from UIO FS anymore. Unless there is a warning or error.\n");

    // Only notify when we have consumed every commands.
    // After printing our finish message to not mess up client's printing.
    notify_vmm();

    // Turn on IRQ
    uio_interrupt_ack(cmd_uio_fd);

    while (1) {
        int n_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n_events == -1) {
            perror("main(): epoll_wait():");
            LOG_FS("epoll wait failed\n");
            exit(EXIT_FAILURE);
        }
        if (n_events == MAX_EVENTS) {
            LOG_FS_WARN("epoll_wait() returned MAX_EVENTS, there maybe dropped events!\n");
        }

        for (int i = 0; i < n_events; i++) {
            assert(events[i].data.fd == cmd_uio_fd);
            process_fs_commands();
            uio_interrupt_ack(cmd_uio_fd);
            notify_vmm();
        }
    }

    LOG_FS_WARN("Exit\n");
    return EXIT_SUCCESS;
}
