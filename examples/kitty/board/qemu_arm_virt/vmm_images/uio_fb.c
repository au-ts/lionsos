#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include "uio.h"

void signal_ready_to_vmm() {
    printf("UIO FB|INFO: ready to receive data, faulting on VMM buffer\n");
    char command_str[64] = {0};
    sprintf(command_str, "devmem %d", UIO_INIT_ADDRESS);
    system(command_str);
}

int get_uio_map_addr(char *path, void **addr) {
    /*****************************************************************************/
    // Get address of map0 from UIO device
    size_t addr_fp = open("/sys/class/uio/uio0/maps/map0/addr", O_RDONLY);
    if (addr_fp == -1) {
        printf("Error opening file uio0 map addr");
        return 1;
    }

    char addr_str[64]; // Buffer to hold the string representation of the address
    int addr_str_values_read = read(addr_fp, addr_str, sizeof(addr_str));

    if (addr_str_values_read <= 0) {
        printf("Error reading from file");
        close(addr_fp);
        return 2;
    }

    if (addr_str_values_read == sizeof(addr_str)) {
        fprintf(stderr, "Address string is too long to fit in address_str buffer\n");
        close(addr_fp);
        return 3;
    }

    addr_str[addr_str_values_read] = '\0'; // Null-terminate the string to be safe
    unsigned long long addr_val; // Use this to hold the actual address value
    sscanf(addr_str, "%llx", &addr_val);
    *addr = (void *)addr_val;

    close(addr_fp);
}

int get_uio_map_size(char *path, size_t *size) {
    /*****************************************************************************/
    // Get size of map0 from UIO device
    size_t size_fp = open("/sys/class/uio/uio0/maps/map0/size", O_RDONLY);
    if (size_fp == -1) {
        printf("Error opening file uio0 map size");
        return 4;
    }

    char size_str[64]; // Buffer to hold the string representation of the size
    int size_str_values_read = read(size_fp, size_str, sizeof(size_str));

    if (size_str_values_read <= 0) {
        printf("Error reading from file");
        close(size_fp);
        return 5;
    }

    if (size_str_values_read == sizeof(size_str)) {
        fprintf(stderr, "Size string is too long to fit in size_str buffer\n");
        close(size_fp);
        return 6;
    }

    size_str[size_str_values_read] = '\0'; // Null-terminate the string to be safe
    sscanf(size_str, "%llx", size);

    close(size_fp);
}

int get_uio_map_offset(char *path, size_t *offset) {
    /*****************************************************************************/
    // Get offset of map0 from UIO device
    size_t offset_fp = open("/sys/class/uio/uio0/maps/map0/offset", O_RDONLY);
    if (offset_fp == -1) {
        printf("Error opening file uio0 map offset");
        return 7;
    }

    char offset_str[64]; // Buffer to hold the string representation of the offset
    int offset_str_values_read = read(offset_fp, offset_str, sizeof(offset_str));

    if (offset_str_values_read <= 0) {
        printf("Error reading from file");
        close(offset_fp);
        return 8;
    }

    if (offset_str_values_read == sizeof(offset_str)) {
        fprintf(stderr, "Offset string is too long to fit in offset_str buffer\n");
        close(offset_fp);
        return 9;
    }

    offset_str[offset_str_values_read] = '\0'; // Null-terminate the string to be safe
    sscanf(offset_str, "%llx", offset);

    close(offset_fp);
}

int main() {
    printf("UIO FB|INFO: Starting...\n");
    void *addr;
    size_t size;
    size_t offset;

    get_uio_map_addr("/sys/class/uio/uio0/maps/map0/addr", &addr);
    get_uio_map_size("/sys/class/uio/uio0/maps/map0/size", &size);
    get_uio_map_offset("/sys/class/uio/uio0/maps/map0/offset", &offset);

    printf("addr: %p\n", addr);
    printf("size: 0x%llx\n", size);
    printf("offset: 0x%llx\n", offset);
    /*****************************************************************************/
    struct fb_var_screeninfo vinfo;
    long int screensize = 0;

    // Open /dev/fb0 device to read config info and allow mmap
    int fb_fp = open("/dev/fb0", O_RDWR);
    if (fb_fp == -1) {
        printf("Error opening file fb0");
        return 10;
    }
    printf("UIO FB|INFO: opened /dev/fb0\n");

    // Get variable screen information
    if (ioctl(fb_fp, FBIOGET_VSCREENINFO, &vinfo) == -1) {
        printf("Error reading variable information");
        return 21;
    }

    // Activate framebuffer
    printf("Activating framebuffer...\n");
    vinfo.activate |= FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
    if (0 > ioctl(fb_fp, FBIOPUT_VSCREENINFO, &vinfo)) {
        printf("Failed to refresh\n");
        return 21;
    }
    printf("Activated framebuffer\n");
    // Figure out the size of the screen in bytes
    screensize = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;

    if (screensize > size) {
        printf("UIO FB|ERROR: screensize is larger than size of uio map0\n");
        return 22;
    }

    void *fbmap = mmap(NULL, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fp, 0);
    printf("UIO FB|INFO: screensize: 0x%x\n", screensize);
    if (fbmap == MAP_FAILED) {
        printf("UIO FB|ERROR: failed to mmap frame buffer: %s\n", strerror(errno));
        return -1;
    }
    printf("UIO FB|INFO: mmaped /dev/fb0\n");

    close(fb_fp);
    /*****************************************************************************/
    // Open UIO device to allow read write and mmap
    int uio_fp = open("/dev/uio0", O_RDWR);
    if (uio_fp == -1) {
        printf("Error opening uio0");
        return 11;
    }
    printf("UIO FB|INFO: opened /dev/uio0\n");

    void *map0 = mmap(addr, size, PROT_READ | PROT_WRITE, MAP_SHARED, uio_fp, offset);

    printf("Finished mmap\n");
    if (map0 == MAP_FAILED) {
        printf("Error mapping uio_map0 memory");
        return 12;
    }

    printf("map0: %p\n", map0);

    /*****************************************************************************/
    // Initialise framebuffer config
    fb_config_t config = {
        .yres = vinfo.yres,
        .xres = vinfo.xres,
        .bpp = vinfo.bits_per_pixel,
    };
    set_fb_config(map0, config);

    void *fb_base;
    get_fb_base_addr(map0, &fb_base);
    /*****************************************************************************/
    uint32_t enable_uio_value = 1; // 4-byte integer value to write to file
    // Enable UIO interrupts first, incase it is already disabled
    if (write(uio_fp, &enable_uio_value, sizeof(uint32_t)) != sizeof(uint32_t)) {
        printf("Error writing to uio0");
        close(uio_fp);
        return 13;
    }
    printf("UIO FB|INFO: UIO INTERRUPT IS ENABLED\n");

    signal_ready_to_vmm();

    // Read from device, this blocks until interrupt
    int32_t read_value;
    int32_t irq_count = 0;
    while (read(uio_fp, &read_value, sizeof(uint32_t)) == sizeof(uint32_t)) {
        if (read_value >= irq_count) {
            // Copy contents of map0 into fbmap
            printf("UIO FB|INFO: Copying from map0 to fbmap\n");
            int ret = memcpy(fbmap, fb_base, screensize);
            if (ret == -1) {
                printf("memcpy error: %s\n", strerror(errno));
            }
            printf("UIO FB|INFO: finished copying\n");
            ret = msync(fbmap, screensize, MS_SYNC);
            if (ret == -1) {
                printf("msync error: %s\n", strerror(errno));
            }
            printf("UIO FB|INFO: finished msyncing fbmap\n");
        }

        irq_count = read_value;

        // Enable interrupts again
        if (write(uio_fp, &enable_uio_value, sizeof(uint32_t)) != sizeof(uint32_t)) {
            printf("UIO FB|ERROR: could not write to uio_fp\n");
            close(uio_fp);
            return 14;
        }

        signal_ready_to_vmm();
    }

    close(uio_fp);
}
