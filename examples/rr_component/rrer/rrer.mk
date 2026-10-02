# Requires the sddf block driver.
# make sure `include ${SDDF}/tools/make/board/common.mk` is done above including this.
RRER_DIR := ${RR_COMPONENT_DIR}/rrer
RRER_CFLAGS := ${CFLAGS} \
			-I${RRER_DIR}/include \
			-I$(SDDF)/include \
			-I$(SDDF)/include/microkit

RRER_LDFLAGS := ${LDFLAGS} -lmicrokit -L$(BOARD_DIR)/lib -L$(SDDF)/lib
RRER_LIBS := $(LIONS_LIBC)/lib/libc.a
export CFLAGS += -DDEBUG_VIRTIO_TRANSPORT

# Dependencies first
UART_DRIV_DIR := virtio
SERIAL_COMPONENTS := $(SDDF)/serial/components
UART_DRIVER := $(SDDF)/drivers/serial/$(UART_DRIV_DIR)
SERIAL_DRIVER := $(SDDF)/drivers/serial/${UART_DRIV_DIR}
RR_SERIAL_O_FILES := \
	serial_driver.o \
	serial_virt_tx.o

BLK_DRIV_DIR := virtio/mmio
PARTITION := # unspecified
RR_BLK_O_FILES := \
	blk_driver.o blk_virt.o

include ${SDDF}/util/util.mk
include ${UART_DRIVER}/serial_driver.mk
include ${SERIAL_COMPONENTS}/serial_components.mk

include ${SDDF}/drivers/blk/${BLK_DRIV_DIR}/blk_driver.mk
include ${SDDF}/blk/components/blk_components.mk

RRER_O_FILES := rr_block_checker.o rr_sender.o rr_recorder.o rr_replayer.o rr_main.o

RRER_BLOCK_CHECKER_DIR := ${RRER_DIR}/block_checker
RRER_SENDER_DIR := ${RRER_DIR}/sender
RRER_RR_DIR := ${RRER_DIR}/rr

RRER_ELF_FILES := $(RRER_O_FILES:.o=.elf) $(RR_SERIAL_O_FILES:.o=.elf) $(RR_BLK_O_FILES:.o=.elf)

# add ourselves to the image list.
# not sure about proper make conventions
IMAGES += ${RRER_ELF_FILES}

.PHONY:: deps rr_main
deps: $(RR_BLK_O_FILES:.o=.elf)

rr_block_checker.o: deps
	${CC} ${RRER_CFLAGS} -c ${RRER_BLOCK_CHECKER_DIR}/main.c -o $@

rr_sender.o: deps
	${CC} ${RRER_CFLAGS} -c ${RRER_SENDER_DIR}/main.c -o $@

rr_sender.elf: rr_sender.o | libsddf_util_debug.a
	${LD} $< ${RRER_LIBS} ${RRER_LDFLAGS} -o $@ libsddf_util_debug.a

rr_block_checker.elf: rr_block_checker.o | libsddf_util_debug.a
	${LD} $< ${RRER_LIBS} ${RRER_LDFLAGS} -o $@ libsddf_util_debug.a

rr_recorder.o: deps
	${CC} ${RRER_CFLAGS} -c ${RRER_RR_DIR}/recorder.c -o $@

rr_recorder.elf: rr_recorder.o | libsddf_util.a libsddf_util_debug.a
	${LD} $< ${RRER_LIBS} ${RRER_LDFLAGS} -o $@ libsddf_util_debug.a

rr_replayer.o: deps
	${CC} ${RRER_CFLAGS} -c ${RRER_RR_DIR}/replayer.c -o $@

rr_replayer.elf: rr_replayer.o | libsddf_util.a libsddf_util_debug.a
	${LD} $< ${RRER_LIBS} ${RRER_LDFLAGS} -o $@ libsddf_util_debug.a

ifndef RR_MODE
	$(error Please specify RR_MODE as one of the following options: recorder replayer)
endif
ifneq ($(RR_MODE),$(filter $(RR_MODE),recorder replayer))
	$(error Please specify RR_MODE (currently "${RR_MODE}") as one of the following options: recorder replayer)
endif

rr_main.elf: rr_replayer.elf rr_recorder.elf
	cp -f rr_${RR_MODE}.elf rr_main.elf

-include $(RRER_O_FILES:.o=.d)
