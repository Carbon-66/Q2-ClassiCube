/* A stand-in for alsa-lib in the host build: the PCM is a file, $SND_OUT (raw 16-bit stereo at
 * 44.1 kHz), and each write takes as long as the frames would play, as a real device paces it. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fd = -1;

int snd_pcm_open(void** pcm, const char* name, int stream, int mode) {
    const char* f = getenv("SND_OUT");
    fd = f && *f ? open(f, O_WRONLY | O_CREAT | O_TRUNC, 0644) : open("/dev/null", O_WRONLY);
    *pcm = &fd;
    return fd < 0 ? -2 : 0;
}
int snd_pcm_set_params(void* pcm, int format, int access, unsigned channels, unsigned rate,
                       int soft_resample, unsigned latency) { return 0; }
long snd_pcm_writei(void* pcm, const void* buf, unsigned long frames) {
    if (write(fd, buf, frames * 4) < 0) return -5;
    usleep((useconds_t)(frames * 1000000ull / 44100));
    return (long)frames;
}
int snd_pcm_recover(void* pcm, int err, int silent) { return 0; }
int snd_pcm_drop(void* pcm) { return 0; }
int snd_pcm_close(void* pcm) { if (fd >= 0) close(fd); fd = -1; return 0; }
