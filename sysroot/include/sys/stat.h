#ifndef _SYS_STAT_H
#define _SYS_STAT_H
#include "../_q2.h"
/* mkdir only: glibc 2.28's stat is an inline over __xstat, so the backend doesn't use it */
int mkdir(const char *, mode_t);
#endif
