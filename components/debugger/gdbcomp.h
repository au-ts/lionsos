#include <microkit.h>
#include <sel4/sel4_arch/types.h>
#define GDB_LOG(...) \ 
	    printf("GDB | " __VA_ARGS__)
#define GDB_ERR(...) \
	do { \
    	printf("GDB_ERR | " __VA_ARGS__); \
    	*(volatile int*)NULL; \
	} while (0) 

typedef struct {
    bool valid;
    char* data;
    seL4_Word size;
    uint8_t cksum; // calculated checksum
    uint8_t tcksum; // transmitted checksum
} gdb_packet_t;

void gdb_init();
void gdb_start();
seL4_Bool gdb_fault(microkit_child ch, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo);
void gdb_notified();
