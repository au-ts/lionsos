# This work is Crown Copyright NCSC, 2024.

# REQUIRED FOR FUNCTIONALITY
ifeq ($(strip $(BUILD_DIR)),)
$(error BUILD_DIR must be specified)
endif

ifeq ($(strip $(MICROKIT_TOOL)),)
$(error MICROKIT_TOOL must be specified)
endif

ifeq ($(strip $(MICROKIT_BOARD)),)
$(error MICROKIT_BOARD must be specified)
endif

ifeq ($(strip $(MICROKIT_CONFIG)),)
$(error MICROKIT_CONFIG must be specified)
endif


include $(CURRENT_EXAMPLE)/Makefile # include the makefile for the example passed in 

# TOOLCHAIN := aarch64-none-elf
TOOLCHAIN := aarch64-linux-gnu

CPU := cortex-a53
CC := $(TOOLCHAIN)-gcc
LD := $(TOOLCHAIN)-ld
AS := $(TOOLCHAIN)-as

# Define general warnings used by all protection domains
DCSS_WARNINGS := -Wall -Wno-comment -Wno-return-type -Wno-unused-function -Wno-unused-value -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-label -Wno-pointer-sign
CLIENT_WARNINGS += -Wall -Wno-comment -Wno-return-type -Wno-unused-function -Wno-unused-value -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-label -Wno-pointer-sign

# List of the object files needed for each protection domain
DCSS_OBJS 		:=  dcss.o timer.o dma.o picolibc_link.o vic_table.o API_general.o test_base_sw.o util.o write_register.o API_AFE_t28hpc_hdmitx.o API_AFE.o vic_table.o API_HDMITX.o API_AVI.o API_Infoframe.o hdmi_tx.o context_loader.o dpr.o dtg.o scaler.o sub_sampler.o
CLIENT_OBJS		+=  api.o timer.o picolibc_link.o vic_table.o frame_buffer.o

# define c flags and includes for the dcss protection domain 
DCSS_INC := $(BOARD_DIR)/include include include/hdmi include/dcss include/util
DCSS_INC_PARAMS=$(foreach d, $(DCSS_INC), -I$d)
DCSS_CFLAGS := -mcpu=$(CPU) -mstrict-align  -nostdlib -nolibc -ffreestanding -g3 -O3 $(DCSS_WARNINGS) $(DCSS_INC_PARAMS) -I$(BOARD_DIR)/include --specs=dep/picolibc/picolibc.specs

# Define separate configuration for the client to avoid code bloat from unused includes
CLIENT_INC += $(BOARD_DIR)/include include include/hdmi include/util include/api
CLIENT_INC_PARAMS=$(foreach d, $(CLIENT_INC), -I$d)
CLIENT_CFLAGS += -mcpu=$(CPU) -mstrict-align  -nostdlib -nolibc -ffreestanding -g3 -O3 $(CLIENT_WARNINGS) $(CLIENT_INC_PARAMS) -I$(BOARD_DIR)/include --specs=dep/picolibc/picolibc.specs

# Microkit lib flags		// TODO move down
LDFLAGS := -L$(BOARD_DIR)/lib
CLIENT_LDFLAGS += -L$(BOARD_DIR)/lib

# ideally we shouldn't need the -L path for libgcc
DCSS_LIBS := -lmicrokit -Tmicrokit.ld -L/usr/lib/gcc-cross/aarch64-linux-gnu/12 -lgcc -Ldep/picolibc -lc  dep/picolibc/libc.a  -L/usr/lib/gcc-cross/aarch64-linux-gnu/12 -lgcc
CLIENT_LIBS += dep/picolibc/libc.a  -lmicrokit -Tmicrokit.ld -L/usr/lib/gcc-cross/aarch64-linux-gnu/12 -lgcc -Ldep/picolibc -lc  -L/usr/lib/gcc-cross/aarch64-linux-gnu/12 -lgcc

# The images for each protetction domain
IMAGES := dcss.elf client.elf

# all target depends on the protection domain images to be built and the build_image target which builds the final image 
all: $(addprefix $(BUILD_DIR)/, $(IMAGES)) build_image

######################################################

# Compile the example client file 
$(BUILD_DIR)/%.o: src/api/%.c Makefile
	$(CC) -c $(CLIENT_CFLAGS) $< -o $@

# Compile the specific example file. This will contain the implementation of the client init() function
$(BUILD_DIR)/%.o: $(CURRENT_EXAMPLE)/%.c Makefile
	$(CC) -c $(CLIENT_CFLAGS) $< -o $@

######################################################

# Compile the files in the hdmi directory
$(BUILD_DIR)/%.o: src/hdmi/%.c Makefile

	$(CC) -c $(DCSS_CFLAGS) $< -o $@

# Compile the dcss files
$(BUILD_DIR)/%.o: src/dcss/%.c Makefile
	$(CC) -c $(DCSS_CFLAGS) $< -o $@

######################################################

# Compile the object file for picolibc
$(BUILD_DIR)/%.o: dep/picolibc/%.c Makefile
	$(CC) -c $(DCSS_CFLAGS) $< -o $@

# Compile the util files
$(BUILD_DIR)/%.o: src/util/%.c Makefile
	$(CC) -c $(DCSS_CFLAGS) $< -o $@

######################################################

# Create elf files for DCSS protection domain
$(BUILD_DIR)/dcss.elf: $(addprefix $(BUILD_DIR)/, $(DCSS_OBJS))
	$(LD) $(LDFLAGS) $^ dep/picolibc/libc.a $(DCSS_LIBS) -o $@

# Create elf files for Client protection domain
$(BUILD_DIR)/client.elf: $(addprefix $(BUILD_DIR)/, $(CLIENT_OBJS))
	$(LD) $(CLIENT_LDFLAGS) $^ dep/picolibc/libc.a $(CLIENT_LIBS) -o $@

######################################################

# define the main image file and the report file
IMAGE_FILE = $(BUILD_DIR)/loader.img
REPORT_FILE = $(BUILD_DIR)/report.txt

# build entire system
build_image: $(IMAGE_FILE) 
$(IMAGE_FILE) $(REPORT_FILE): sel4-hdmi.system 
	$(MICROKIT_TOOL) sel4-hdmi.system --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

# (optional) clean
clean:
	rm -f $(BUILD_DIR)/*.o $(BUILD_DIR)/*.elf $(BUILD_DIR)/.depend*
	find . -name \*.o |xargs --no-run-if-empty rm
