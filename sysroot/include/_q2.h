/* Shared types for q2-classicube's minimal glibc 2.28 headers for the Shanling Q2 (MIPS32r2
 * little-endian, o32 ABI). There is no MIPS sysroot in the build: these declare only what
 * ClassiCube and the Q2 backend use, with the o32 ABI (32-bit long, time_t and off_t) and MIPS
 * numbering where it differs from x86. */
#ifndef Q2_SYSROOT_H
#define Q2_SYSROOT_H
typedef unsigned int size_t;
typedef int ssize_t;
typedef long off_t;
typedef long time_t;
typedef int pid_t;
typedef unsigned int mode_t;
#ifndef NULL
#define NULL ((void *)0)
#endif
#endif
