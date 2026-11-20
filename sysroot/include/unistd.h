#ifndef _UNISTD_H
#define _UNISTD_H
#include "_q2.h"
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define R_OK 4
#define STDERR_FILENO 2
ssize_t read(int, void *, size_t);
ssize_t write(int, const void *, size_t);
int close(int);
off_t lseek(int, off_t, int);
int access(const char *, int);
int chdir(const char *);
int unlink(const char *);
int rmdir(const char *);
int fsync(int);
int usleep(unsigned);
int dup2(int, int);
#endif
