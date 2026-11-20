#ifndef _PTHREAD_H
#define _PTHREAD_H
/* pthread_create only; the thread is given no attributes */
typedef unsigned long pthread_t;
int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
#endif
