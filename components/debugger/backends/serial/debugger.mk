export DEBUGGER_OBJS := debugger.o
export DEBUGGER_DEPS := $(DEBUGGER_OBJS:.o=.d)
export DEBUGGER_LIBS := $(LIONS_LIBC)/lib/libc.a

debugger.o: $(DEBUGGER_DIR)/backends/serial/debugger.c | $(LIONS_LIBC)/lib/libc.a
	$(CC) $(CFLAGS) -I $(DEBUGGER_DIR) -c -o $@ $^
