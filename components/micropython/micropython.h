#pragma once

#include <lions/libco.h>
#include <stdint.h>
#include <microkit.h>

extern cothread_t t_event;
extern cothread_t t_mp;

#ifdef ENABLE_FRAMEBUFFER
#define FRAMEBUFFER_VMM_CH 0
#endif
#define TIMER_CH 1
#define ETH_RX_CH 2
#define ETH_TX_CH 3
#define NFS_CH 7
#define SERIAL_RX_CH 8
#define SERIAL_TX_CH 9
#ifdef ENABLE_I2C
#define I2C_CH 10
#endif

void await(microkit_channel event_ch);
