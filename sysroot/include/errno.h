#ifndef _ERRNO_H
#define _ERRNO_H
int *__errno_location(void) __attribute__((const));
#define errno (*__errno_location())
#define ENOENT 2
#define EINTR 4
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EBUSY 16
#define EEXIST 17
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENOSPC 28
#define EPIPE 32
#endif
