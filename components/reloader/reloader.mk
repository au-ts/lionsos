RELOADER_DIR := $(LIONSOS)/components/reloader
LIBVSPACE_DIR := $(LIONSOS)/components/reloader/libvspace

$(info our board is: $(BOARD))
BOARD := qemu_virt_aarch64

CFLAGS_reloader := ${CFLAGS}
CFLAGS_reloader += -I$(LIBVSPACE_DIR)
CFLAGS_reloader += -I$(MICROKIT_SDK)/board/$(BOARD)/$(MICROKIT_CONFIG)/include \
    -Iinclude \
    -Iarch_include

include $(LIBVSPACE_DIR)/libvspace.mk

reloader.o: $(RELOADER_DIR)/reloader.c $(LIBVSPACE_DIR)/libvspace.a
	$(CC) -c $(CFLAGS_reloader) -I$(LIBVSPACE_DIR) $< -o $@

# we will just leave this stuff here for another try later
reloader.elf: reloader.o $(LIBVSPACE_DIR)/libvspace.a
	$(LD) $(LDFLAGS) -o $@ $(LIBS) $^

reloader:
	mkdir -p $@

# this is for the example when we want faulting_pd
faulting_pd.elf: faulting_pd.o
	$(LD) $(LDFLAGS) -o $@ $(LIBS) $^

faulting_pd.o: $(RELOADER_DIR)/faulting_pd.c
	$(CC) -c $(CFLAGS_reloader) -I$(LIBVSPACE_DIR) $< -o $@