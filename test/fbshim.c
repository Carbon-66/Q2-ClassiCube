/* LD_PRELOAD for host runs of q2-classicube's real main(): /dev/fb0 becomes a memory file shaped
 * like the Q2's panel (320 x 375, 32 bpp, red at bit 16, two pages). Each FBIOPAN_DISPLAY counts a
 * frame; when $FB_DUMP is set, the page shown is written there (raw BGRA, 320 x 375; with
 * $FB_DUMP_EVERY, only every Nth frame), and at exit
 * the page left showing and the frame count go to $FB_FINAL ("yoffset N pans N"). */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <unistd.h>

#define W 320
#define H 375
static int fbfd = -1;
static unsigned yoff, pans;
static unsigned char* mem;

int open(const char* path, int flags, ...) {
    va_list ap;
    va_start(ap, flags);
    int mode = va_arg(ap, int);
    va_end(ap);
    if (!strcmp(path, "/dev/fb0")) {
        fbfd = memfd_create("fb0", 0);
        if (ftruncate(fbfd, W * 4 * H * 2)) return -1;
        return fbfd;
    }
    int (*real)(const char*, int, ...) = dlsym(RTLD_NEXT, "open");
    return real(path, flags, mode);
}

void* mmap(void* a, size_t n, int prot, int fl, int fd, off_t off) {
    void* (*real)(void*, size_t, int, int, int, off_t) = dlsym(RTLD_NEXT, "mmap");
    void* p = real(a, n, prot, fl, fd, off);
    if (fd == fbfd && fd >= 0) mem = p;
    return p;
}

static void final(void) {
    const char* f = getenv("FB_FINAL");
    FILE* o = f ? fopen(f, "w") : 0;
    if (o) fprintf(o, "yoffset %u pans %u\n", yoff, pans), fclose(o);
}

int ioctl(int fd, unsigned long req, ...) {
    va_list ap;
    va_start(ap, req);
    unsigned* arg = va_arg(ap, unsigned*);
    va_end(ap);
    if (fd != fbfd || fd < 0) {
        int (*real)(int, unsigned long, ...) = dlsym(RTLD_NEXT, "ioctl");
        return real(fd, req, arg);
    }
    if (req == 0x4600) { /* FBIOGET_VSCREENINFO */
        memset(arg, 0, 160);
        arg[0] = W, arg[1] = H, arg[2] = W, arg[3] = 2 * H, arg[5] = yoff, arg[6] = 32, arg[8] = 16;
        static int once;
        if (!once++) atexit(final);
        return 0;
    }
    if (req == 0x4602) { /* FBIOGET_FSCREENINFO */
        memset(arg, 0, 68);
        arg[5] = W * 4 * H * 2, arg[11] = W * 4;
        return 0;
    }
    if (req == 0x80044620) { /* FBIO_WAITFORVSYNC, MIPS's number as the game uses it: a 60 Hz panel */
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        long long ns = t.tv_sec * 1000000000LL + t.tv_nsec, period = 1000000000LL / 60;
        long long wait = period - ns % period;
        t.tv_sec = wait / 1000000000LL, t.tv_nsec = wait % 1000000000LL;
        nanosleep(&t, 0);
        return 0;
    }
    if (req == 0x4606) { /* FBIOPAN_DISPLAY */
        /* FB_HANG_AT=N: the Nth swap never returns, as a driver hang would (the stall watcher's test) */
        const char* hang = getenv("FB_HANG_AT");
        if (hang && pans + 1 == (unsigned)atoi(hang)) for (;;) pause();
        yoff = arg[5], ++pans;
        const char* f = getenv("FB_DUMP");
        const char* every = getenv("FB_DUMP_EVERY"); /* write only every Nth frame (the benchmark) */
        if (every && pans % (unsigned)atoi(every)) f = 0;
        char tmp[512];
        snprintf(tmp, sizeof tmp, "%s.part", f ? f : "");
        FILE* o = f && *f && mem ? fopen(tmp, "wb") : 0;
        if (o) fwrite(mem + yoff * W * 4, W * 4, H, o), fclose(o), rename(tmp, f); /* whole frames only */
        return 0;
    }
    return -1;
}
