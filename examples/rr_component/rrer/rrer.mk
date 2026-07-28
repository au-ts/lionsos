RRER_DIR := ${RR_COMPONENT_DIR}/rrer
LIBPMU_DIR := $(RRER_DIR)/libpmu

RRER_CFLAGS := ${CFLAGS} -I${LIBPMU_DIR}
RRER_LDFLAGS := ${LDFLAGS}
RRER_LIBS := libgdb.a libvspace.a libsddf_util_debug.a $(LIONS_LIBC)/lib/libc.a libpmu.a

rrer.o: ${RRER_DIR}/main.c
	${CC} ${RRER_CFLAGS} -c -o $@ $<

rrer.elf: rrer.o ${RRER_LIBS}
	${LD} ${RRER_LDFLAGS} -o $@ $^ ${RRER_LIBS} -lmicrokit -Tmicrokit.ld
# make libpmu
libpmu.a: pmu.o
	${AR} rcs $@ $<

pmu.o: ${LIBPMU_DIR}/pmu.c
	${CC} ${RRER_CFLAGS} -c -o $@ $<
