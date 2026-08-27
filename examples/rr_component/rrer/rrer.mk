RRER_DIR := ${RR_COMPONENT_DIR}/rrer
LIBPMU_DIR := $(RRER_DIR)/libpmu

RRER_CFLAGS := ${CFLAGS} -I${LIBPMU_DIR}
RRER_LDFLAGS := ${LDFLAGS}
RRER_LIBS := libsddf_util_debug.a $(LIONS_LIBC)/lib/libc.a

rrer.o: ${RRER_DIR}/main.c
	${CC} ${RRER_CFLAGS} -c -o $@ $<

rrer.elf: rrer.o ${RRER_LIBS}
	${LD} ${RRER_LDFLAGS} -o $@ $^ ${RRER_LIBS} -lmicrokit -Tmicrokit.ld

-include rrer.d
