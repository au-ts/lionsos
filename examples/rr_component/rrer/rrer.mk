RRER_DIR := ${RR_COMPONENT_DIR}/rrer

RRER_CFLAGS := ${CFLAGS} -I${RRER_DIR}/include
RRER_LDFLAGS := ${LDFLAGS}
RRER_LIBS := libsddf_util_debug.a $(LIONS_LIBC)/lib/libc.a -lmicrokit

RRER_O_FILES := rr_block_checker.o rr_sender.o rr_main.o
RRER_BLOCK_CHECKER_DIR := ${RRER_DIR}/block_checker
RRER_SENDER_DIR := ${RRER_DIR}/sender
RRER_RR_DIR := ${RRER_DIR}/rr

.PHONY:: rr_block_checker_entry rr_sender_entry
rr_block_checker_entry: rr_block_checker.elf
	$(eval block_checker_entry=$(shell llvm-readelf rr_block_checker.elf --file-header | grep "Entry point" | awk '{print $$4}'))

rr_sender_entry: rr_sender.elf
	$(eval sender_entry=$(shell llvm-readelf rr_sender.elf --file-header | grep "Entry point" | awk '{print $$4}'))

rr_block_checker.o:
	@echo "sender_entry: ${sender_entry}"
	@echo "block_checker_entry: ${block_checker_entry}"
	${CC} ${RRER_CFLAGS} -c ${RRER_BLOCK_CHECKER_DIR}/main.c -o $@

rr_main.o: rr_block_checker_entry rr_sender_entry
	${CC} ${RRER_CFLAGS} -DSENDER_ENTRY_POINT=${sender_entry} -DBLOCK_CHECKER_ENTRY_POINT=${block_checker_entry} -c ${RRER_RR_DIR}/main.c -o $@

rr_sender.o:
	${CC} ${RRER_CFLAGS} -c ${RRER_SENDER_DIR}/main.c -o $@

%.elf: %.o
	${LD} $< ${RRER_LIBS} ${RRER_LDFLAGS} -o $@

-include $(RRER_O_FILES:.o=.d)
