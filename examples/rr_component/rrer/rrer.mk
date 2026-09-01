RRER_DIR := ${RR_COMPONENT_DIR}/rrer

RRER_CFLAGS := ${CFLAGS} -I${RRER_DIR}/include
RRER_LDFLAGS := ${LDFLAGS}
RRER_LIBS := libsddf_util_debug.a $(LIONS_LIBC)/lib/libc.a -lmicrokit

RRER_O_FILES := rr_block_checker.o rr_sender.o rr_main.o
RRER_BLOCK_CHECKER_DIR := ${RRER_DIR}/block_checker
RRER_SENDER_DIR := ${RRER_DIR}/sender
RRER_RR_DIR := ${RRER_DIR}/rr

rr_block_checker.o:
	${CC} ${RRER_CFLAGS} -c ${RRER_BLOCK_CHECKER_DIR}/main.c -o $@

rr_main.o:
	${CC} ${RRER_CFLAGS} -c ${RRER_RR_DIR}/main.c -o $@

rr_sender.o:
	${CC} ${RRER_CFLAGS} -c ${RRER_SENDER_DIR}/main.c -o $@

%.elf: %.o
	${LD} $< ${RRER_LIBS} ${RRER_LDFLAGS} -o $@

-include $(RRER_O_FILES:.o=.d)
