#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H
#include "../_q2.h"
/* MIPS numbering: SOCK_DGRAM is 1 and SOCK_STREAM 2, the other way round from x86 */
typedef unsigned int socklen_t;
typedef unsigned short sa_family_t;
struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};
#define AF_UNIX 1
#define SOCK_DGRAM 1
#define SOCK_CLOEXEC 0x80000
#define MSG_DONTWAIT 0x40
int socket(int, int, int);
int bind(int, const void *, socklen_t);
ssize_t recv(int, void *, size_t, int);
#endif
