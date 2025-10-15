/* This work is Crown Copyright NCSC, 2024. */

#include <stdint.h>

void initialise_and_start_timer(uintptr_t);
unsigned long timer_get_ms(void);
void ms_delay(int);
void u_delay(int delay);
void start_timer();
int stop_timer();