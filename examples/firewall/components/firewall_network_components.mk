#
# Copyright 2023, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
# This Makefile snippet builds the firewall network components
# (for example, simple RX and TX virtualisers)
# it should be included into your project Makefile
#
# NOTES:
# Generates firewall_network_virt_rx.elf firewall_network_virt_tx.elf
# Requires ${SDDF}/util/util.mk to build the utility library for debug output

FIREWALL_NETWORK_COMPONENTS_DIR := $(abspath $(dir $(lastword ${MAKEFILE_LIST})))
FIREWALL_NETWORK_IMAGES:= firewall_network_virt_rx.elf firewall_network_virt_tx.elf
firewall_network/components/%.o: ${FIREWALL_COMPONENTS}/%.c
	${CC} ${CFLAGS} -c -o $@ $<

FIREWALL_NETWORK_COMPONENT_OBJ := $(addprefix firewall_network/components/, network_virt_tx.o network_virt_rx.o)

CHECK_FIREWALL_NETWORK_FLAGS_MD5:=.firewall_network_cflags-$(shell echo -- ${CFLAGS} ${CFLAGS_network} | shasum | sed 's/ *-//')

${CHECK_FIREWALL_NETWORK_FLAGS_MD5}:
	-rm -f .firewall_network_cflags-*
	touch $@

#vpath %.c ${SDDF}/network/components


${FIREWALL_NETWORK_IMAGES}: LIBS := libsddf_util_debug.a ${LIBS}

${FIREWALL_NETWORK_COMPONENT_OBJ}: |firewall_network/components
${FIREWALL_NETWORK_COMPONENT_OBJ}: ${CHECK_FIREWALL_NETWORK_FLAGS_MD5}
${FIREWALL_NETWORK_COMPONENT_OBJ}: CFLAGS+=${CFLAGS_FIREWALL_NETWORK}

firewall_network/components/firewall_network_virt_%.o: ${SDDF}/firewall_network/components/virt_%.c
	${CC} ${CFLAGS} -c -o $@ $<

%.elf: firewall_network/components/%.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

clean::
	${RM} -f firewall_network_virt_[rt]x.[od]

clobber::
	${RM} -f ${FIREWALL_NETWORK_IMAGES}
	rmdir firewall_network/components

firewall_network/components:
	mkdir -p $@

-include ${FIREWALL_NETWORK_COMPONENTS_OBJS:.o=.d}
