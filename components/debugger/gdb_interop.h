#include <microkit.h>
#include <sel4/sel4_arch/types.h>
#ifdef DEBUGGER__DEBUG_MODE
#define DEBUGGER_LOG(...) \ 
	    printf("DEBUGGER | " __VA_ARGS__)
#else 
#define DEBUGGER_LOG(...)
#endif
#define DEBUGGER_ERR(...) \
	do { \
    	printf("DEBUGGER_ERR | " __VA_ARGS__); \
    	*(volatile int*)NULL; \
	} while (0) 

typedef struct {
    bool valid;
    char* data;
    seL4_Word size;
    uint8_t cksum; // calculated checksum
    uint8_t tcksum; // transmitted checksum
} debugger_packet_t;

void debugger_init();
void debugger_start();
seL4_Bool debugger_fault(microkit_child ch, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo);
void debugger_notified();
