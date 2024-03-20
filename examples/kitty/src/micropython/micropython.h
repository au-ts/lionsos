#pragma once

#include <libco.h>
#include <stdint.h>

extern cothread_t t_event;
extern cothread_t t_mp;

#define FRAMEBUFFER_VMM_CH 0
#define TIMER_CH 1
#define ETH_RX_CH 2
#define ETH_TX_CH 3
#define NFS_CH 7
#define SERIAL_RX_CH 8
#define SERIAL_TX_CH 9
#define I2C_CH 10
#define ETH_ARP_CH 11

enum {
	mp_event_source_none = 0,
	mp_event_source_timer = 1,
	mp_event_source_serial = 2,
	mp_event_source_network = 4,
	mp_event_source_i2c = 8,
    mp_event_source_framebuffer = 16,
	mp_event_source_nfs = 32,
};

// bitwise-OR of currently active event sources
extern int active_events;

// bitwise-OR of event sources micropython is current blocked on
// micropython should set this before switching to the event thread
// and reset it after control is switched back to it
extern int mp_blocking_events;

void await(int event_source);
