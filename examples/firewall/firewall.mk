MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
FIREWALL:=${LIONSOS}/examples/firewall
UTIL:=$(SDDF)/util
ETHERNET_DRIVER_0:=$(SDDF)/drivers/network/$(DRIV_DIR_0)
ETHERNET_DRIVER_1:=$(SDDF)/drivers/network/$(DRIV_DIR_1)
ETHERNET_CONFIG_INCLUDE_0:=${FIREWALL}/include/ethernet_config_imx
ETHERNET_CONFIG_INCLUDE_1:=${FIREWALL}/include/ethernet_config_dwmac-5.10a
SERIAL_COMPONENTS := $(SDDF)/serial/components
UART_DRIVER := $(SDDF)/drivers/serial/$(UART_DRIV_DIR)
SERIAL_CONFIG_INCLUDE:=${FIREWALL}/include/serial_config
BENCHMARK:=${FIREWALL}/benchmark
NETWORK_COMPONENTS:=$(SDDF)/network/components

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
SYSTEM_FILE := ${FIREWALL}/firewall.system
IMAGE_FILE := loader.img
REPORT_FILE := report.txt

vpath %.c ${SDDF} ${FIREWALL}

IMAGES := eth_driver_0.elf eth_driver_1.elf network_virt_rx_0.elf network_virt_rx_1.elf \
	  network_virt_tx_0.elf network_virt_tx_1.elf copy_0.elf copy_1.elf forwarder.elf \
	  benchmark.elf idle.elf serial_virt_tx.elf uart_driver.elf

CFLAGS := -mcpu=$(CPU) \
	  -mstrict-align \
	  -ffreestanding \
	  -g3 -O3 -Wall \
	  -Wno-unused-function \
	  -DMICROKIT_CONFIG_$(MICROKIT_CONFIG) \
	  -I$(BOARD_DIR)/include \
	  -I$(SDDF)/include \
	  -I$(SERIAL_CONFIG_INCLUDE) \
	  -MD \
	  -MP

LDFLAGS := -L$(BOARD_DIR)/lib -L${LIBC}
LIBS := --start-group -lmicrokit -Tmicrokit.ld -lc libsddf_util_debug.a --end-group

CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@

all: loader.img

${IMAGES}: libsddf_util_debug.a

${IMAGE_FILE} $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

include ${SDDF}/util/util.mk
include ${SERIAL_COMPONENTS}/serial_components.mk
include ${UART_DRIVER}/uart_driver.mk

# common settings for eth0 and eth1
CHECK_NETWORK_FLAGS_MD5:=.network_cflags-$(shell echo -- ${CFLAGS} ${CFLAGS_network} | shasum | sed 's/ *-//')
${CHECK_NETWORK_FLAGS_MD5}:
	-rm -f .network_cflags-*
	touch $@

network/components: 
	mkdir -p network/components

# generate network components for eth0
NETWORK_IMAGES_0:= network_virt_rx_0.elf network_virt_tx_0.elf copy_0.elf forwarder_0.elf
${NETWORK_IMAGES_0}: LIBS := libsddf_util_debug.a ${LIBS}

network/components/eth0/%.o: ${SDDF}/network/components/%.c
	mkdir -p network/components/eth0
	${CC} ${CFLAGS} -I${ETHERNET_CONFIG_INCLUDE_0} -c -o $@ $<

network/components/eth0/forwarder.o: ${FIREWALL}/forwarder.c
	mkdir -p network/components/eth0
	${CC} ${CFLAGS} -I${ETHERNET_CONFIG_INCLUDE_0} -c -o $@ $<

NETWORK_COMPONENT_OBJ_0 := $(addprefix network/components/eth0/, copy.o network_virt_tx.o network_virt_rx.o forwarder.o)

${NETWORK_COMPONENT_OBJ_0}: |network/components
${NETWORK_COMPONENT_OBJ_0}: ${CHECK_NETWORK_FLAGS_MD5}
${NETWORK_COMPONENT_OBJ_0}: CFLAGS+=${CFLAGS_network}

network/components/eth0/network_virt_%.o: ${SDDF}/network/components/virt_%.c 
	mkdir -p network/components/eth0
	${CC} ${CFLAGS} -I${ETHERNET_CONFIG_INCLUDE_0} -c -o $@ $<

forwarder_0.elf: network/components/eth0/forwarder.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

%_0.elf: network/components/eth0/%.o forwarder_0.elf
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

# generate network components for eth1
NETWORK_IMAGES_1:= network_virt_rx_1.elf network_virt_tx_1.elf copy_1.elf
${NETWORK_IMAGES_1}: LIBS := libsddf_util_debug.a ${LIBS}

network/components/eth1/%.o: ${SDDF}/network/components/%.c
	mkdir -p network/components/eth1
	${CC} ${CFLAGS} -I${ETHERNET_CONFIG_INCLUDE_1} -c -o $@ $<

network/components/eth1/forwarder.o: ${FIREWALL}/forwarder.c
	mkdir -p network/components/eth1
	${CC} ${CFLAGS} -I${ETHERNET_CONFIG_INCLUDE_1} -c -o $@ $<

NETWORK_COMPONENT_OBJ_1 := $(addprefix network/components/eth1/, copy.o network_virt_tx.o network_virt_rx.o forwarder.o)

${NETWORK_COMPONENT_OBJ_1}: |network/components
${NETWORK_COMPONENT_OBJ_1}: ${CHECK_NETWORK_FLAGS_MD5}
${NETWORK_COMPONENT_OBJ_1}: CFLAGS+=${CFLAGS_network}

network/components/eth1/network_virt_%.o: ${SDDF}/network/components/virt_%.c 
	mkdir -p network/components/eth1
	${CC} ${CFLAGS} -I${ETHERNET_CONFIG_INCLUDE_1} -c -o $@ $<

forwarder_1.elf: network/components/eth1/forwarder.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

%_1.elf: network/components/eth1/%.o forwarder_1.elf
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

# generate eth0 driver 
imx/ethernet.o: ${ETHERNET_DRIVER_0}/ethernet.c ${CHECK_NETDRV_FLAGS_MD5}
	mkdir -p imx
	${CC} -c ${CFLAGS} ${CFLAGS_network} -I${ETHERNET_CONFIG_INCLUDE_0} -I ${ETHERNET_DRIVER_0} -o $@ $<

eth_driver_0.elf: imx/ethernet.o
	$(LD) $(LDFLAGS) $< $(LIBS) -o $@

# generate eth1 driver 
dwmac-5.10a/ethernet.o: ${ETHERNET_DRIVER_1}/ethernet.c ${CHECK_NETDRV_FLAGS}
	mkdir -p dwmac-5.10a
	${CC} -c ${CFLAGS} ${CFLAGS_network} -I${ETHERNET_CONFIG_INCLUDE_1} -I ${ETHERNET_DRIVER_1} -o $@ $<

eth_driver_1.elf: dwmac-5.10a/ethernet.o
	$(LD) $(LDFLAGS) $< $(LIBS) -o $@


BENCH_OBJS := benchmark/benchmark.o
IDLE_OBJS := benchmark/idle.o
LIBUTIL_DBG := libsddf_util_debug.a
LIBUTIL := libsddf_util.a

# benchmark code 
benchmark/%.o: ${BENCHMARK}/%.c
	mkdir -p benchmark
	${CC} -c ${CFLAGS} -c -o $@ $<

${BENCH_OBJS} ${IDLE_OBJS}: ${CHECK_FLAGS_BOARD_MD5} |benchmark
benchmark:
	mkdir -p benchmark

benchmark.elf: $(BENCH_OBJS) ${LIBUTIL}
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

idle.elf: $(IDLE_OBJS) ${LIBUTIL_DBG}
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

clean::
	${RM} -f *.elf .depend* $
	find . -name \*.[do] |xargs --no-run-if-empty rm

clobber:: clean
	rm -f *.a
	rm -f ${IMAGE_FILE} ${REPORT_FILE}

-include $(DEPS)
