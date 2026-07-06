export DEBUGGER_OBJS := debugger.o
export DEBUGGER_DEPS := $(DEBUGGER_OBJS:.o=.d)
export DEBUGGER_LIBS := 

debugger.o: $(DEBUGGER_DIR)/backends/serial/debugger.c
	$(CC) $(CFLAGS) -I $(DEBUGGER_DIR) -c -o $@ $^
