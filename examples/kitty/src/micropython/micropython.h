#pragma once

#include <libco.h>
#include <stdint.h>

extern cothread_t t_event;
extern cothread_t t_mp;

#define VMM_CH 0
#define TIMER_CH 1
#define NFS_CH 7

enum {
	mp_event_source_none = 0,
	mp_event_source_timer = 1,
	mp_event_source_uart = 2,
	mp_event_source_network = 4,
	mp_event_source_nfc = 8,
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
