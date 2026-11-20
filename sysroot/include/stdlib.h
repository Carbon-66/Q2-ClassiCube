#ifndef _STDLIB_H
#define _STDLIB_H
#include "_q2.h"
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
void exit(int) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
char *getenv(const char *);
#endif
