PDCONTROL := $(dir $(abspath $(lastword ${MAKEFILE_LIST})))
PDC_PYPATH := ${PDCONTROL}
PDC_include := ${PDCONTROL}/include
PDC_src := ${PDCONTROL}/src
PDC_cflags := -MD \
	-mstrict-align \
	-ffreestanding \
	${OPTIMISATION} \
	-Wall \
	${CFLAGS_ARCH} \
	-I ${BOARD_DIR}/include \
	-I ${PDC_include} \
	-I ${LIONS_LIBC}/include \
	-I ${SDDF}/include \
	-I$(SDDF)/include/microkit

PDC_ldflags := -l${LIONS_LIBC}/lib/libc.a

CFLAGS += -I${PDC_include}
PDC_c_files := $(wildcard ${PDC_src}/*.c)
PDC_o_files := $(patsubst ${PDC_src}/%.c,%.o,${PDC_c_files})

${PDC_o_files}: | ${LIONS_LIBC}/include
	${CC} -c ${PDC_cflags} ${PDC_ldflags} $(patsubst %.o,${PDC_src}/%.c,$@) -o $@

libpdcontrol.a: ${PDC_o_files}
	${AR} rcs $@ $^ -lc

-include ${PDC_o_files:.o=.d}
