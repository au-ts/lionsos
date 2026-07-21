RRER_DIR := ${RR_COMPONENT_DIR}/rrer
rrer.o: ${RRER_DIR}/main.c
	${CC} ${CFLAGS} -c -o $@ $<

rrer.elf: rrer.o
	${LD} ${LDFLAGS} -o $@ $^ ${LIBS}
