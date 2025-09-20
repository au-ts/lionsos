/* This work is Crown Copyright NCSC, 2024. */

#include "dma_offsets.h"
#include "context_loader.h"
#include "dma.h"
#include "timer.h"
#include "hdmi_data.h"
#include "write_register.h"

#include "dpr.h"
#include "dtg.h"
#include "scaler.h"

#include <microkit.h>

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#define CTXLD_CTRL_STATUS 0x23000
#define DB_BASE_ADDR 0x23010
#define DB_COUNT 0x23014

#define ARB_SEL 1
#define ENABLE 0

int context = 0; // This keeps track of the current context.

void init_context_loader(uintptr_t dma_base, uintptr_t dcss_base, struct hdmi_data *hdmi_config, uint32_t* active_frame_buffer_offset, uint32_t* cache_frame_buffer_offset) {

	// Steps 1 and 2 of 15.4.2.2 Display state loading sequence are done here as the double buffered registers do not need to change what they contain.
	// So it should only be written once.
    
	uintptr_t* frame_buffer1_addr = getPhys((void*)dma_base);   
	uintptr_t* frame_buffer2_addr = getPhys((void*)dma_base + FRAME_BUFFER_TWO_OFFSET);

	/*  
		The context loader has access to two double buffered registers depening on the current context.
		These registers are 64 bit and hold the address of the frame buffer in the first 32 bits and
		the DPR memory register where the frame buffer will be set in the second 32 bits. 
		See 15.4.2.3 System Memory Display state format
	*/
	uint32_t* ctx_ld_db1_addr = (uint32_t*)(dma_base + CTX_LD_DB_ONE_ADDR); 	
	*ctx_ld_db1_addr = (uintptr_t)frame_buffer1_addr;							
	ctx_ld_db1_addr++; 															
	*ctx_ld_db1_addr = dcss_base + DPR_1_FRAME_1P_BASE_ADDR_CTRL0; 
	
	uint32_t* ctx_ld_db2_addr = (uint32_t*)(dma_base + CTX_LD_DB_TWO_ADDR); 	
	*ctx_ld_db2_addr = (uintptr_t)frame_buffer2_addr;							
	ctx_ld_db2_addr++; 															
	*ctx_ld_db2_addr = dcss_base + DPR_1_FRAME_1P_BASE_ADDR_CTRL0; 
	
	run_context_loader(dma_base, dcss_base, hdmi_config, active_frame_buffer_offset, cache_frame_buffer_offset);
}


void run_context_loader(uintptr_t dma_base, uintptr_t dcss_base, struct hdmi_data *hdmi_config, uint32_t* active_frame_buffer_offset, uint32_t* cache_frame_buffer_offset){
	
	// Steps 3,4,5 and 12 of 15.4.2.2 Display state loading sequence
	start_timer();

	uint32_t* enable_status = (uint32_t*)(dcss_base + CTXLD_CTRL_STATUS);
	
	// Give priority to the context loader TODO: Probably only needs to be done once per initialisation
	*enable_status = set_bit(*enable_status, ARB_SEL);

	// Set the context offset in memory for the current frame buffer to display
	int contex_offset = (context == 0) ? CTX_LD_DB_ONE_ADDR : CTX_LD_DB_TWO_ADDR;

	// STEP 3 waiting until its idle (it will almost definitely just be idle already, but this is here just to follow the spec)
	int context_ld_enabled = read_bit(*enable_status, ENABLE);
	
	while (context_ld_enabled == 1) {																			
		context_ld_enabled = read_bit(*enable_status, ENABLE);
		seL4_Yield();	
	}	

	// STEP 4 write the double buffered registers (values set previously in init_context_loader)
	// Set the base adress for the double buffered context
	write_register((uint32_t*)(dcss_base + DB_BASE_ADDR), (uintptr_t)getPhys((void*)dma_base + contex_offset));
	write_register((uint32_t*)(dcss_base + DB_COUNT), 2);	
	
	// STEP 5 Set the context loader status to enable
	// Set the enable status bit to 1 to kickstart process.
	*enable_status = set_bit(*enable_status, ENABLE);												
	context_ld_enabled = read_bit(*enable_status, ENABLE); 
	
	// Poll contiously until context loader is not being used.
	while (context_ld_enabled == 1) {																			
		context_ld_enabled = read_bit(*enable_status, ENABLE);
		seL4_Yield();	
	}

	// Set the dma offset for the current framebuffer to be used by the client
	*active_frame_buffer_offset = (context == 0) ? FRAME_BUFFER_TWO_OFFSET : FRAME_BUFFER_ONE_OFFSET;
	*cache_frame_buffer_offset = (context == 0) ?  FRAME_BUFFER_ONE_OFFSET : FRAME_BUFFER_TWO_OFFSET;
	context = context == 1 ? 0 : 1; 																			

	// Notify the client to draw the frame buffer
	microkit_notify(52);
	int time_elapsed = stop_timer();

	if (hdmi_config->ms_delay != NO_DELAY) {
		int delay_time = hdmi_config->ms_delay - time_elapsed;
		if (delay_time > 0) {
			ms_delay(delay_time);
		}
	}
}