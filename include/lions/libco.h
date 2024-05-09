#ifndef LIBCO_H
#define LIBCO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void* cothread_t;

cothread_t co_active(void);
cothread_t co_derive(void*, unsigned int, void (*)(void));
void co_switch(cothread_t);

#ifdef __cplusplus
}
#endif

/* ifndef LIBCO_H */
#endif
