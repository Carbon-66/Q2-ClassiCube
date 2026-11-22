/* q2-classicube's own interfaces between the window (Window_Q2.c), the platform (Platform_Q2.c)
 * and the host tests (test/). */
#ifndef Q2_CLASSICUBE_H
#define Q2_CLASSICUBE_H
#include "Core.h"
#include "Bitmap.h"

#define Q2_VERSION    "1.23"
#define Q2_APP_DIR    "/mnt/mmc/.classicube"
#define Q2_INPUT_SOCK "/tmp/q2-classicube.input.sock"
#define Q2_OPT_UI_SCALE "q2-uiscale"
#define Q2_UI_SCALE     0.58f

/* Window_Q2.c */
int  Q2_FbOpen(void);
void Q2_FbClose(void);
void Q2_FbLog(void);
void Q2_Blit(unsigned char* page, unsigned line, const BitmapCol* src, int w, int h, int stride);
void Q2_Input(const char* datagram);
void Q2_InputClose(void);
int  Q2_KeyFor(int code, cc_bool menu);
int  Q2_GameAction(int code, cc_bool down);
void Q2_KeyboardStep(int dir);

/* Audio_Q2.c: the sound output's arguments (argv[1] and argv[2]; none without audio=1) */
extern const char* Q2_SoundDevice;
extern const char* Q2_SoundVolume;
int  Q2_SoundGain(const char* volume);
void Q2_Mix(cc_int16* out, int frames);

/* Platform_Q2.c */
void    Q2_NoteInput(void);
cc_bool Q2_QuitAsked(void);
/* A decimal integer at s (leading spaces and a sign allowed); *end, if given, is just past it */
int     Q2_ParseInt(const char* s, const char** end);

/* What the main thread is doing (a constant string) and its frame count, for the stall watcher
 * (Platform_Q2.c), which logs them and every thread's place in the kernel when frames stop */
extern volatile const char* Q2_Phase;
extern volatile cc_uint32   Q2_Frames;
#endif
