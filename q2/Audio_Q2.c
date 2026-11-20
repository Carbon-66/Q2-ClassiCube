/* ClassiCube's sounds on the Shanling Q2 under Q2 Pod: an audio backend that mixes the playing
 * sounds on a thread of its own and writes them to ALSA, following Q2 Pod's contract for a card
 * game with audio=1 (docs/internals.md "A game with the sound", docs/librespot.md):
 *
 *   - Q2 Pod passes the output as argv[1]: "plughw:0,0" (the headphone DAC), "plug:bluealsa" or
 *     "usb" (the card /proc/asound/cards lists as USB-Audio, plughw:N,0); and off the jack
 *     argv[2], the volume: "NN" (0-100, applied here on hciplayer's Bluetooth curve) or "hNN"
 *     (the DAC applies it, so this plays at unity).
 *   - The music player lets go of the PCM a moment after Q2 Pod stops it, so the open is
 *     retried: 2 s on the jack, 5 s elsewhere, and snd_pcm_set_params while bluealsa answers
 *     EBUSY, 5 s more.
 *   - On the jack, the DAC's PCM mode and mute are cleared through /dev/shanling_dac after the
 *     open, and the mute set again when the PCM closes.
 *   - Mono Audio and Swap L/R follow Q2 Pod's /tmp/q2mono and /tmp/q2swap (an int each).
 *
 * The device is opened on the mixer thread, so the game starts while it waits. ClassiCube plays
 * each sound on one of AudioPool's contexts; here a context is a voice the mixer steps through
 * at the sound's rate and pitch, resampled to the output's 44.1 kHz stereo. Music is off
 * (CC_BUILD_NOMUSIC): its streams aren't supported.
 *
 * ClassiCube's sounds are quiet (a median peak of 14% of full scale, the loudest 34%), so they're
 * played louder, q2-soundboost percent (400 by default), and the mix is compressed gently past
 * three quarters of full scale, so sounds playing together don't clip hard. */
#include "Core.h"

#if CC_AUD_BACKEND == CC_AUD_BACKEND_Q2
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

struct AudioContext { int count, voice, volume; };

#include "_AudioBase.h"
#include "Funcs.h"
#include "Options.h"
#include "q2.h"

/* alsa-lib (libasound.so.2), the few calls used */
int  snd_pcm_open(void** pcm, const char* name, int stream, int mode);
int  snd_pcm_set_params(void* pcm, int format, int access, unsigned channels, unsigned rate,
                        int soft_resample, unsigned latency_us);
long snd_pcm_writei(void* pcm, const void* buffer, unsigned long frames);
int  snd_pcm_recover(void* pcm, int err, int silent);
int  snd_pcm_drop(void* pcm);
int  snd_pcm_close(void* pcm);
#define SND_PCM_STREAM_PLAYBACK 0
#define SND_PCM_FORMAT_S16_LE 2
#define SND_PCM_ACCESS_RW_INTERLEAVED 3

#define RATE    44100
#define CHUNK   256    /* frames a write, about 6 ms */
#define VOICES  8      /* AudioPool's contexts */
#define LATENCY_JACK  60000
#define LATENCY_OTHER 200000 /* Bluetooth needs the slack, as Q2 Pod's video player gives it */
#define DAC       "/dev/shanling_dac"
#define DAC_PCM   0xc0044d1bu
#define DAC_MUTE  0xc0044d1fu
#define MONO_FILE "/tmp/q2mono"
#define SWAP_FILE "/tmp/q2swap"
#define BOOST_DEFAULT 400
#define KNEE 24576 /* past this the mix rises a quarter as fast, to full scale at 4x the rest */

const char* Q2_SoundDevice; /* argv[1] and argv[2], from main() */
const char* Q2_SoundVolume;

static struct Voice {
	const cc_int16* data;
	cc_uint32 frames, pos, step; /* pos and step in 1/65536 frames */
	int channels, volume;        /* volume: 0-256 */
	int active;
} voices[VOICES];

static struct {
	int started, opened, failed, stop, closed;
	int gain;                    /* 1/65536; 65536 where the DAC applies the volume */
	void* pcm;
	int dac;
	volatile int* mono;
	volatile int* swap;
	char lock;                   /* voices[]: the mixer's chunk against a sound's start or stop */
	int nextVoice;
	int boost;                   /* percent */
} mix = { 0, 0, 0, 0, 0, 65536, NULL, -1 };

static void Lock(void)   { while (__atomic_test_and_set(&mix.lock, __ATOMIC_ACQUIRE)) { } }
static void Unlock(void) { __atomic_clear(&mix.lock, __ATOMIC_RELEASE); }

static int ParseInt(const char* s) {
	int v = 0;
	while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
	return v;
}

/* hciplayer's Bluetooth soft volume for Q2 Pod's v (0-100), as Q2 Pod's video player has it:
 * 0.002 v under 50, then 0.1 + 0.018 (v - 50), 1 at 100. "hNN", or none, is unity. */
int Q2_SoundGain(const char* volume) {
	int v;
	if (!volume || volume[0] == 'h') return 65536;
	v = ParseInt(volume);
	if (v > 100) v = 100;
	return 65536 * (v < 50 ? 2 * v : 18 * v - 800) / 1000;
}


/*########################################################################################################################*
*---------------------------------------------------------Mixer-----------------------------------------------------------*
*#########################################################################################################################*/
/* Every playing voice into out, n stereo frames */
void Q2_Mix(cc_int16* out, int frames) {
	static int acc[CHUNK * 2];
	int i, f, n;
	Mem_Set(acc, 0, sizeof(int) * 2 * frames);

	Lock();
	for (i = 0; i < VOICES; i++)
	{
		struct Voice* v = &voices[i];
		if (!v->active) continue;

		for (f = 0; f < frames; f++)
		{
			cc_uint32 at = v->pos >> 16;
			int l, r;
			if (at >= v->frames) { v->active = 0; break; }

			if (v->channels == 2) {
				l = v->data[at * 2];
				r = v->data[at * 2 + 1];
			} else {
				l = r = v->data[at];
			}
			acc[f * 2]     += l * v->volume >> 8;
			acc[f * 2 + 1] += r * v->volume >> 8;
			v->pos += v->step;
		}
	}
	Unlock();

	for (n = 0; n < frames * 2; n++)
	{
		int s = acc[n];
		if (s >  KNEE) s =  KNEE + (( s - KNEE) >> 2);
		if (s < -KNEE) s = -KNEE - ((-s - KNEE) >> 2);
		out[n] = (cc_int16)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
	}
}

/* /tmp/q2mono and /tmp/q2swap: Q2 Pod's ints, mapped once they exist */
static volatile int* MapFlag(volatile int* have, const char* path) {
	int fd;
	void* p;
	if (have) return have;
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) return NULL;
	p = mmap(NULL, sizeof(int), PROT_READ, MAP_SHARED, fd, 0);
	close(fd);
	return p == MAP_FAILED ? NULL : (volatile int*)p;
}

/* "usb": the card /proc/asound/cards lists as USB-Audio (" N [id   ]: USB-Audio - name") */
static void UsbDevice(char* out) {
	char b[1024];
	int fd = open("/proc/asound/cards", O_RDONLY | O_CLOEXEC), n = 0, card = 1;
	char* u;
	if (fd >= 0) { n = (int)read(fd, b, sizeof(b) - 1); close(fd); }
	b[n > 0 ? n : 0] = '\0';

	u = strstr(b, "USB-Audio");
	if (u) {
		while (u > b && u[-1] != '\n') u--;
		while (*u == ' ') u++;
		card = ParseInt(u);
	}
	memcpy(out, "plughw:", 7);
	out[7] = (char)('0' + card % 10);
	memcpy(out + 8, ",0", 3);
}

static int SoundOpen(void) {
	char dev[64];
	int err = 0, off = 0, i, jack;

	if (!strcmp(Q2_SoundDevice, "usb")) {
		UsbDevice(dev);
	} else {
		for (i = 0; Q2_SoundDevice[i] && i < (int)sizeof(dev) - 1; i++) dev[i] = Q2_SoundDevice[i];
		dev[i] = '\0';
	}
	jack = !strcmp(dev, "plughw:0,0");

	for (i = 0; i < (jack ? 20 : 50) && !mix.stop; i++)
	{
		if (!(err = snd_pcm_open(&mix.pcm, dev, SND_PCM_STREAM_PLAYBACK, 0))) break;
		mix.pcm = NULL;
		usleep(100000);
	}
	if (!mix.pcm) { Platform_Log2("Sound: can't open %c (error %i)", dev, &err); return 0; }

	/* S16_LE, interleaved, stereo, resampled as needed; bluealsa claims its PCM here */
	for (i = 0; ; i++)
	{
		err = snd_pcm_set_params(mix.pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, RATE, 1,
								 jack ? LATENCY_JACK : LATENCY_OTHER);
		if (err != -EBUSY || jack || i >= 50 || mix.stop) break;
		usleep(100000);
	}
	if (err) {
		Platform_Log2("Sound: %c: set_params error %i", dev, &err);
		snd_pcm_close(mix.pcm);
		mix.pcm = NULL;
		return 0;
	}

	if (jack && (mix.dac = open(DAC, O_RDWR | O_CLOEXEC)) >= 0) {
		ioctl(mix.dac, DAC_PCM,  &off);
		ioctl(mix.dac, DAC_MUTE, &off);
	}
	Platform_Log1("Sound: %c at 44100 Hz", dev);
	return 1;
}

static void SoundClose(void) {
	int on = 1;
	if (mix.pcm) { snd_pcm_drop(mix.pcm); snd_pcm_close(mix.pcm); mix.pcm = NULL; }
	if (mix.dac >= 0) { ioctl(mix.dac, DAC_MUTE, &on); close(mix.dac); mix.dac = -1; }
}

static void* Mixer(void* arg) {
	static cc_int16 buf[CHUNK * 2];
	int i, ticks = 0;

	if (!SoundOpen()) {
		__atomic_store_n(&mix.failed, 1, __ATOMIC_RELEASE);
		__atomic_store_n(&mix.closed, 1, __ATOMIC_RELEASE);
		return NULL;
	}
	__atomic_store_n(&mix.opened, 1, __ATOMIC_RELEASE);

	while (!__atomic_load_n(&mix.stop, __ATOMIC_ACQUIRE))
	{
		int gain, mono, swap;
		long w;
		if (!(ticks++ & 127)) { /* Q2 Pod makes the files once the settings are first changed */
			mix.mono = MapFlag(mix.mono, MONO_FILE);
			mix.swap = MapFlag(mix.swap, SWAP_FILE);
		}
		Q2_Mix(buf, CHUNK);

		gain = mix.gain;
		mono = mix.mono && *mix.mono;
		swap = mix.swap && *mix.swap;
		if (gain != 65536 || mono || swap) {
			for (i = 0; i < CHUNK * 2; i += 2)
			{
				int l = buf[i] * gain >> 16, r = buf[i + 1] * gain >> 16, t;
				if (mono) l = r = (l + r) / 2;
				if (swap) { t = l; l = r; r = t; }
				buf[i] = (cc_int16)l; buf[i + 1] = (cc_int16)r;
			}
		}

		w = snd_pcm_writei(mix.pcm, buf, CHUNK);
		if (w < 0 && snd_pcm_recover(mix.pcm, (int)w, 1) < 0) usleep(20000);
	}

	SoundClose();
	__atomic_store_n(&mix.closed, 1, __ATOMIC_RELEASE);
	return NULL;
}


/*########################################################################################################################*
*---------------------------------------------------------Backend---------------------------------------------------------*
*#########################################################################################################################*/
/* Sounds need Q2 Pod's sound output: argv[1], passed with q2app's audio=1 */
cc_bool AudioBackend_Init(void) {
	pthread_t t;
	if (mix.started) return !mix.failed;
	if (!Q2_SoundDevice || !Q2_SoundDevice[0] || !strcmp(Q2_SoundDevice, "-")) return false;

	mix.started = 1;
	mix.gain    = Q2_SoundGain(Q2_SoundVolume);
	mix.boost   = Options_GetInt("q2-soundboost", 0, 1000, BOOST_DEFAULT);
	if (pthread_create(&t, NULL, Mixer, NULL)) { mix.failed = 1; return false; }
	return true;
}

void AudioBackend_Tick(void) { }

/* The mixer closes the PCM and mutes the DAC: waited for, up to 300 ms */
void AudioBackend_Free(void) {
	int i;
	if (!mix.started || mix.failed) return;
	__atomic_store_n(&mix.stop, 1, __ATOMIC_RELEASE);
	for (i = 0; i < 30 && !__atomic_load_n(&mix.closed, __ATOMIC_ACQUIRE); i++) usleep(10000);
}

cc_result Audio_Init(struct AudioContext* ctx, int buffers) {
	if (!ctx->voice) {
		if (mix.nextVoice >= VOICES) return ERR_NOT_SUPPORTED;
		ctx->voice = ++mix.nextVoice; /* 1-based: 0 is none yet */
	}
	ctx->count  = buffers;
	ctx->volume = 100;
	return 0;
}

void Audio_Close(struct AudioContext* ctx) {
	if (ctx->voice) {
		Lock();
		voices[ctx->voice - 1].active = 0;
		Unlock();
	}
	ctx->count = 0;
}

void Audio_SetVolume(struct AudioContext* ctx, int volume) { ctx->volume = volume; }


/*########################################################################################################################*
*------------------------------------------------------Stream context-----------------------------------------------------*
*#########################################################################################################################*/
/* Music's: not played here */
cc_result StreamContext_SetFormat(struct AudioContext* ctx, int channels, int sampleRate, int playbackRate) {
	return ERR_NOT_SUPPORTED;
}
cc_result StreamContext_Enqueue(struct AudioContext* ctx, struct AudioChunk* chunk) { return ERR_NOT_SUPPORTED; }
cc_result StreamContext_Play(struct AudioContext* ctx)  { return ERR_NOT_SUPPORTED; }
cc_result StreamContext_Pause(struct AudioContext* ctx) { return ERR_NOT_SUPPORTED; }
cc_result StreamContext_Update(struct AudioContext* ctx, int* inUse) { return ERR_NOT_SUPPORTED; }


/*########################################################################################################################*
*------------------------------------------------------Sound context------------------------------------------------------*
*#########################################################################################################################*/
/* Any sound plays on any voice: the mixer resamples */
cc_bool SoundContext_FastPlay(struct AudioContext* ctx, struct AudioData* data) { return true; }

cc_result SoundContext_PlayData(struct AudioContext* ctx, struct AudioData* data) {
	struct Voice* v;
	int channels = data->channels == 2 ? 2 : 1;
	cc_uint32 rate = (cc_uint32)data->sampleRate * (cc_uint32)data->rate / 100;
	if (!ctx->voice || mix.failed || !rate) return 0; /* no output: nothing to hear */

	v = &voices[ctx->voice - 1];
	Lock();
	v->data     = (const cc_int16*)data->chunk.data;
	v->channels = channels;
	v->frames   = data->chunk.size / (2 * channels);
	v->pos      = 0;
	v->step     = (cc_uint32)(((cc_uint64)rate << 16) / RATE);
	v->volume   = min(max(ctx->volume, 0), 100) * mix.boost * 256 / 10000;
	v->active   = 1;
	Unlock();
	return 0;
}

cc_result SoundContext_PollBusy(struct AudioContext* ctx, cc_bool* isBusy) {
	*isBusy = ctx->voice && voices[ctx->voice - 1].active;
	return 0;
}


/*########################################################################################################################*
*--------------------------------------------------------Audio misc-------------------------------------------------------*
*#########################################################################################################################*/
cc_bool Audio_DescribeError(cc_result res, cc_string* dst) { return false; }
#endif
