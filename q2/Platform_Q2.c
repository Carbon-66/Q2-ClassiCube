/* ClassiCube on the Shanling Q2 under Q2 Pod: the platform (files, time, logging) and main().
 *
 * Q2 Pod runs .classicube/q2-classicube from the card with the sound output's arguments (q2app has
 * audio=1, for the media buttons).
 * Everything ClassiCube keeps is in that folder: options.txt, texpacks/, maps/, screenshots/ and
 * classicube.log. The world is saved to maps/q2-world.cw when the game ends (Quit, Return held,
 * the idle limit) and every AUTOSAVE_MS, and loaded again at the next start.
 *
 * There's no MIPS sysroot in the build: sysroot/include declares the libc used here, and
 * tools/stubs.py makes stub libc.so.6 and libm.so.6 with the real sonames, so the Q2's own glibc
 * 2.28 runs. glibc 2.28's stat() is an inline over __xstat, so nothing here calls it: opendir tells
 * a folder from a file, and lseek gives a file's length. */
#define CC_NO_UPDATER
#define CC_NO_DYNLIB
#define CC_NO_SOCKETS
#define CC_NO_THREADING
#define CC_NO_OPEN
#define CC_NO_CRASHHANDLER
#define CC_NO_ENCRYPTION

#include "Stream.h"
#include "ExtMath.h"
#include "Funcs.h"
#include "Window.h"
#include "Utils.h"
#include "Errors.h"
#include "Options.h"
#include "Game.h"
#include "Server.h"
#include "World.h"
#include "Formats.h"
#include "Deflate.h"
#include "Chat.h"
#include "Logger.h"

#include <dirent.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "q2.h"

#define LOG_FILE      "classicube.log"
#define LOG_PREVIOUS  "classicube-previous.log" /* the last run's: kept when it crashed or was killed */
#define MEMORY_LOG_MS (30 * 1000)
#define WORLD_FILE    "maps/q2-world.cw"
#define WORLD_TEMP    "maps/q2-world.cw.part"
#define AUTOSAVE_MS   (5 * 60 * 1000)
#define IDLE_QUIT_MS  (5 * 60 * 1000) /* untouched this long: save and end, so a forgotten game doesn't keep the screen lit */

int rename(const char* from, const char* to);

const cc_result ReturnCode_FileShareViolation = 1000000000; /* not used */
const cc_result ReturnCode_FileNotFound     = ENOENT;
const cc_result ReturnCode_PathNotFound     = ENOENT;
const cc_result ReturnCode_DirectoryExists  = EEXIST;

const char* Platform_AppNameSuffix  = " Q2";
cc_bool Platform_ReadonlyFilesystem = false;
cc_uint8 Platform_Flags = PLAT_FLAG_SINGLE_PROCESS | PLAT_FLAG_APP_EXIT;
#include "_PlatformBase.h"


/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
static int log_fd = STDERR_FILENO;

void Platform_Log(const char* msg, int len) {
	if (write(log_fd, msg, len) < 0) return;
	if (write(log_fd, "\n", 1) < 0) return;
}

/*########################################################################################################################*
*----------------------------------------------Diagnostics: crashes and memory--------------------------------------------*
*#########################################################################################################################*/
/* Q2 Pod notes nothing when a card game dies, so the log says it: a crash's signal and where, the
 * kernel's notes about the game or a lack of memory (the last run's, when the Q2 wasn't restarted
 * since: the out-of-memory killer ends a process without a word to it), and the memory in use
 * now and then. The last run's log is kept as classicube-previous.log. */
static int TextLen(const char* s) { int n = 0; while (s[n]) n++; return n; }

static char* PutText(char* dst, const char* s) { while (*s) *dst++ = *s++; return dst; }
static char* PutHex(char* dst, cc_uint32 v) {
	int i;
	for (i = 28; i >= 0; i -= 4) *dst++ = "0123456789abcdef"[(v >> i) & 15];
	return dst;
}

/* A crash: the signal, the address it was about, and (MIPS) the instruction and the caller's return
 * address, to look up in build/q2-classicube.debug (llvm-addr2line -f -e). Only write() in here. */
static void OnCrash(int sig, siginfo_t* info, void* context) {
	char line[160], *p = line;
	cc_uint32 pc = 0, ra = 0;
#ifdef __mips__
	/* the kernel's o32 ucontext: flags, link, stack (12 bytes), then sigcontext (8-byte aligned, at
	 * 24): regmask, status, pc (64-bit), regs[32] (64-bit); little-endian, the low half first */
	const cc_uint8* uc = (const cc_uint8*)context;
	pc = *(const cc_uint32*)(uc + 24 + 8);
	ra = *(const cc_uint32*)(uc + 24 + 16 + 31 * 8);
#endif
	p = PutText(p, "Crashed: signal ");
	*p++ = '0' + sig / 10; *p++ = '0' + sig % 10;
	p = PutText(p, " (");
	p = PutText(p, sig == SIGSEGV ? "bad memory access" : sig == SIGBUS ? "bus error" :
				   sig == SIGILL  ? "illegal instruction" : sig == SIGFPE ? "arithmetic" : "abort");
	p = PutText(p, ") at address ");
	p = PutHex(p, (cc_uint32)(cc_uintptr)info->si_addr);
	p = PutText(p, ", pc ");
	p = PutHex(p, pc);
	p = PutText(p, ", ra ");
	p = PutHex(p, ra);
	*p++ = '\n';
	if (write(log_fd, line, p - line) < 0) return;
	/* SA_RESETHAND: returning runs the faulting instruction again, which now ends the process */
}

static void CatchCrashes(void) {
	static const int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
	struct sigaction sa;
	int i;
	Mem_Set(&sa, 0, sizeof(sa));
	sa.sa_sigaction = OnCrash;
	sa.sa_flags     = SA_SIGINFO | SA_RESETHAND;
	for (i = 0; i < Array_Elems(sigs); i++) sigaction(sigs[i], &sa, NULL);
}

/* The stall watcher: a thread that, when the main thread has drawn no frame for 5 s (and again at
 * 20 s and 60 s), logs what it was doing (Q2_Phase) and, for each of the game's threads, its state
 * and the kernel function it waits in (/proc/self/task/N/stat and wchan): a freeze in the screen's
 * or the card's driver shows there. A freeze of the whole Q2 stops this thread too: then the log
 * just ends, which says that. */
volatile const char* Q2_Phase = "starting";
volatile cc_uint32   Q2_Frames;

static int  ReadSmall(const char* path, char* buf, int size);
static void LogMemory(const char* when);
static void Heartbeat(const char* note);
static cc_uint64 NowMicros(int clock);
#define BEAT_MS (10 * 1000)

static void LogThreads(void) {
	static char path[64], stat[512], wchan[96];
	DIR* dir = opendir("/proc/self/task");
	struct dirent* e;
	if (!dir) return;
	while ((e = readdir(dir))) {
		const char* name = e->d_name;
		const char* s;
		char state = '?';
		if (name[0] < '0' || name[0] > '9') continue;

		{
			char* p = path;
			p = PutText(p, "/proc/self/task/"); p = PutText(p, name); p = PutText(p, "/stat"); *p = '\0';
			ReadSmall(path, stat, sizeof(stat));
			p = path;
			p = PutText(p, "/proc/self/task/"); p = PutText(p, name); p = PutText(p, "/wchan"); *p = '\0';
			if (!ReadSmall(path, wchan, sizeof(wchan))) PutText(wchan, "?")[0] = '\0';
		}
		/* the state is the letter after the name's closing bracket */
		for (s = stat + TextLen(stat); s > stat && *s != ')'; s--) { }
		if (*s == ')' && s[1] == ' ') state = s[2];
		Platform_Log3("  thread %c: state %r, waiting in %c", name, &state, wchan);
	}
	closedir(dir);
}

static void* StallWatch(void* arg) {
	cc_uint32 last = Q2_Frames;
	int still = 0, said = 0, ticks = 0;
	for (;;)
	{
		usleep(1000 * 1000);
		if (++ticks % (BEAT_MS / 1000) == 0) Heartbeat("running");
		if (Q2_Frames != last) {
			if (said) Platform_Log1("Frames again after %i s", &still);
			last = Q2_Frames; still = 0; said = 0;
			continue;
		}
		still++;
		if (still == 5 || still == 20 || still == 60) {
			const char* phase = (const char*)Q2_Phase;
			Heartbeat("NO FRAMES");
			Platform_Log2("No frame for %i s; the game was %c. Its threads:", &still, phase);
			LogThreads();
			LogMemory("stalled");
			said = 1;
		}
	}
	return NULL;
}

/* A file's first few KB (/proc files have no length) */
static int ReadSmall(const char* path, char* buf, int size) {
	int fd = open(path, O_RDONLY | O_CLOEXEC), len = 0, n;
	if (fd < 0) return 0;
	while (len < size - 1 && (n = read(fd, buf + len, size - 1 - len)) > 0) len += n;
	close(fd);
	buf[len] = '\0';
	return len;
}

/* The number after key in the text, in kB as /proc gives it, or -1 */
static int FindKB(const char* text, const char* key) {
	const char* at = strstr(text, key);
	if (!at) return -1;
	at += TextLen(key);
	while (*at == ' ' || *at == '\t') at++;
	return Q2_ParseInt(at, NULL);
}

/* The Q2's health: its temperatures (every thermal zone the kernel has), the battery and the load:
 * the game keeps both cores busy, the kernel has no cpufreq (so no throttling), and a freeze of the
 * whole Q2 after a few minutes could be heat or the battery's voltage sagging. Into buf, one line. */
static int ReadNumber(const char* path) {
	char text[32];
	if (!ReadSmall(path, text, sizeof(text))) return -1000000;
	return Q2_ParseInt(text, NULL);
}

static void Health(char* buf, int size) {
	static char path[80], load[64];
	cc_string str = { buf, 0, (cc_uint16)(size - 1) };
	int i, t, mv, pct, ma;
	for (i = 0; i < 4; i++)
	{
		char* p = PutText(path, "/sys/class/thermal/thermal_zone");
		*p++ = '0' + i;
		PutText(p, "/temp")[0] = '\0';
		t = ReadNumber(path);
		if (t == -1000000) break;
		/* millidegrees, or degrees on some drivers */
		if (t > 1000 || t < -1000) t /= 1000;
		String_Format2(&str, "zone %i %i C, ", &i, &t);
	}
	if (!i) {
		/* no thermal zone: a hardware monitor's, if there is one */
		for (i = 0; i < 4; i++)
		{
			char* p = PutText(path, "/sys/class/hwmon/hwmon");
			*p++ = '0' + i;
			PutText(p, "/temp1_input")[0] = '\0';
			t = ReadNumber(path);
			if (t == -1000000) break;
			if (t > 1000 || t < -1000) t /= 1000;
			String_Format2(&str, "sensor %i %i C, ", &i, &t);
		}
		if (!i) String_AppendConst(&str, "no CPU temperature, ");
	}

	/* the charge from the Q2's fuel gauge (CW2015), as Q2 Pod's battery icon reads it: the PMIC's
	 * own capacity (power_supply/battery/capacity) stays near 100 and is no use */
	pct = ReadNumber("/sys/devices/i2c-0/0-0062/cw2015_capacity");
	mv  = ReadNumber("/sys/class/power_supply/battery/voltage_now");
	if (pct != -1000000) String_Format1(&str, "battery %i percent", &pct);
	else                 String_AppendConst(&str, "battery charge unknown");
	if (mv != -1000000) {
		if (mv > 100000) mv /= 1000; /* microvolts */
		String_Format1(&str, ", %i mV", &mv);
	}
	ma = ReadNumber("/sys/class/power_supply/battery/current_now");
	if (ma != -1000000) {
		if (ma > 100000 || ma < -100000) ma /= 1000; /* microamps */
		String_Format1(&str, ", %i mA", &ma);
	}
	t = ReadNumber("/sys/class/power_supply/battery/temp");
	if (t != -1000000) {
		int whole = t / 10;  /* tenths of a degree */
		String_Format1(&str, ", %i C", &whole);
	}
	if (ReadSmall("/sys/class/power_supply/battery/status", load, sizeof(load))) {
		for (i = 0; load[i] && load[i] != '\n'; i++) { }
		load[i] = '\0';
		String_Format1(&str, ", %c", load);
	}
	String_AppendConst(&str, "; ");

	if (ReadSmall("/proc/loadavg", load, sizeof(load))) {
		for (i = 0; load[i] && load[i] != '\n'; i++) { }
		load[i] = '\0';
		String_Format1(&str, "load %c", load);
	}
	buf[str.length] = '\0';
}

/* Frames a second since the last note, and how the two cores met (Graphics_SoftGPU.c) */
void Q2_TwoCoreStats(int* handoffs, int* workerSleeps, int* mainSleeps, int* mainWaitMs);
int  Q2_GetCores(void);
static void LogSpeed(cc_uint32 ms) {
	static cc_uint32 lastFrames;
	cc_uint32 frames = Q2_Frames - lastFrames;
	int fps10 = ms ? (int)((cc_uint64)frames * 10000 / ms) : 0, whole = fps10 / 10, tenth = fps10 % 10;
	int handoffs, workerSleeps, mainSleeps, waitMs, waitPct, view = Game_ViewDistance;
	lastFrames = Q2_Frames;
	Q2_TwoCoreStats(&handoffs, &workerSleeps, &mainSleeps, &waitMs);
	Platform_Log3("Speed: %i.%i frames a second, view distance %i", &whole, &tenth, &view);
	if (Q2_GetCores() != 2 || !frames) return;
	handoffs /= (int)frames;
	waitPct = ms ? waitMs * 100 / (int)ms : 0;
	Platform_Log3("  two cores: %i hand-offs a frame; since the last note the second core slept %i times, the main %i", &handoffs, &workerSleeps, &mainSleeps);
	Platform_Log1("  the main core waited for the second %i percent of the time", &waitPct);
}

static void LogHealth(void) {
	static char line[256];
	Health(line, sizeof(line));
	Platform_Log1("Health: %c", line);
}

/* The heartbeat: every 10 s the last few health lines go to the Q2's own flash (/mnt/data, written
 * at once), not the card, and a clean end removes the file. At the next start, a file still there
 * means that run didn't end: its lines, the last before the freeze, go into the log. Written there,
 * they survive a card that stopped answering, which the log on the card wouldn't. */
#define BEAT_FILE  "/mnt/data/q2-classicube.beat"
#define BEAT_LINES 12
static char beat_lines[BEAT_LINES][192];
static int  beat_count;

static void WriteBeatFile(void) {
	static char all[BEAT_LINES * 200];
	char* p = all;
	int i, fd;
	for (i = beat_count > BEAT_LINES ? beat_count - BEAT_LINES : 0; i < beat_count; i++)
	{
		p = PutText(p, beat_lines[i % BEAT_LINES]);
		*p++ = '\n';
	}
	fd = open(BEAT_FILE, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd < 0) return;
	if (write(fd, all, p - all) < 0) { }
	close(fd);
}

static void Heartbeat(const char* note) {
	static char health[160];
	char* line = beat_lines[beat_count % BEAT_LINES];
	cc_string str = { line, 0, sizeof(beat_lines[0]) - 1 };
	cc_uint32 secs = (cc_uint32)(NowMicros(CLOCK_MONOTONIC) / 1000000);
	cc_uint32 frames = Q2_Frames;
	const char* phase = (const char*)Q2_Phase;
	Health(health, sizeof(health));
	String_Format4(&str, "%i s: %c; frames %i, %c; ", &secs, note, &frames, phase);
	String_AppendConst(&str, health);
	line[str.length] = '\0';
	beat_count++;
	WriteBeatFile();
}

static void BeatsFromLastRun(void) {
	static char text[BEAT_LINES * 200];
	int i, len = ReadSmall(BEAT_FILE, text, sizeof(text));
	if (len <= 0) return;
	Platform_LogConst("The last run didn't end (a freeze or a forced restart). Its last notes, from the Q2's flash:");
	for (i = 0; i < len; )
	{
		int j = i;
		while (j < len && text[j] != '\n') j++;
		text[j] = '\0';
		Platform_Log1("  %c", text + i);
		i = j + 1;
	}
	unlink(BEAT_FILE);
}

static void LogMemory(const char* when) {
	static char status[4096], meminfo[2048];
	int rss, peak, avail, total;
	ReadSmall("/proc/self/status", status, sizeof(status));
	ReadSmall("/proc/meminfo", meminfo, sizeof(meminfo));
	rss   = FindKB(status,  "VmRSS:")        / 1024;
	peak  = FindKB(status,  "VmHWM:")        / 1024;
	avail = FindKB(meminfo, "MemAvailable:") / 1024;
	total = FindKB(meminfo, "MemTotal:")     / 1024;
	Platform_Log1("Memory, %c:", when);
	Platform_Log4("  the game %i MB (at most %i MB); available %i MB of %i MB", &rss, &peak, &avail, &total);
}

/* Lines of the kernel's log (since the Q2 started) about this game, or about memory running out */
static void LogKernelNotes(void) {
	static char rec[2048], kept[8][200];
	int fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	int count = 0, i, n, len;
	if (fd < 0) { Platform_LogConst("The kernel's log can't be read"); return; }

	for (i = 0; i < 100000; i++)
	{
		const char* msg;
		len = read(fd, rec, sizeof(rec) - 1);
		if (len < 0 && errno == EPIPE) continue; /* records overwritten while reading */
		if (len <= 0) break;
		rec[len] = '\0';
		for (msg = rec; *msg && *msg != ';'; msg++) { }
		if (*msg) msg++;

		if (!strstr(msg, "q2-classicube") && !strstr(msg, "Out of memory") &&
			!strstr(msg, "oom-kill")      && !strstr(msg, "page allocation failure")) continue;
		for (n = 0; n < 199 && msg[n] && msg[n] != '\n'; n++) kept[count % 8][n] = msg[n];
		kept[count % 8][n] = '\0';
		count++;
	}
	close(fd);

	if (!count) { Platform_LogConst("The kernel's log has nothing about this game or memory running out"); return; }
	Platform_LogConst("The kernel's log, about this game or memory running out (the last 8 at most):");
	for (i = count > 8 ? count - 8 : 0; i < count; i++) Platform_Log1("  %c", kept[i % 8]);
}

static cc_uint64 NowMicros(int clock) {
	struct timespec t;
	clock_gettime(clock, &t);
	return (cc_uint64)t.tv_sec * 1000000 + (cc_uint64)(t.tv_nsec / 1000);
}

#define UNIX_EPOCH_SECONDS 62135596800ULL
TimeMS DateTime_CurrentUTC(void) {
	return (cc_uint64)time(NULL) + UNIX_EPOCH_SECONDS;
}

void DateTime_CurrentLocal(struct cc_datetime* t) {
	struct tm loc;
	time_t s = time(NULL);
	localtime_r(&s, &loc);

	t->year   = loc.tm_year + 1900;
	t->month  = loc.tm_mon  + 1;
	t->day    = loc.tm_mday;
	t->hour   = loc.tm_hour;
	t->minute = loc.tm_min;
	t->second = loc.tm_sec;
}

cc_uint64 Stopwatch_Measure(void) { return NowMicros(CLOCK_MONOTONIC); }

cc_uint64 Stopwatch_ElapsedMicroseconds(cc_uint64 beg, cc_uint64 end) {
	return end < beg ? 0 : end - beg;
}

static cc_uint32 NowMS(void) { return (cc_uint32)(NowMicros(CLOCK_MONOTONIC) / 1000); }


/*########################################################################################################################*
*-----------------------------------------------------Directory/File------------------------------------------------------*
*#########################################################################################################################*/
/* Paths are relative to the app's folder, the current directory */
void Platform_EncodePath(cc_filepath* dst, const cc_string* path) {
	char* str = dst->buffer;
	String_EncodeUtf8(str, path);
}

void Platform_DecodePath(cc_string* dst, const cc_filepath* path) {
	const char* str = path->buffer;
	String_AppendUtf8(dst, str, String_Length(str));
}

void Directory_GetCachePath(cc_string* path) { }

cc_result Directory_Create2(const cc_filepath* path) {
	return mkdir(path->buffer, 0777) == -1 ? errno : 0;
}

static int IsDirectory(const char* path) {
	DIR* d = opendir(path);
	if (!d) return 0;
	closedir(d);
	return 1;
}

int File_Exists(const cc_filepath* path) {
	return access(path->buffer, R_OK) == 0 && !IsDirectory(path->buffer);
}

cc_result Directory_Enum(const cc_string* dirPath, void* obj, Directory_EnumCallback callback) {
	cc_string path; char pathBuffer[FILENAME_SIZE];
	cc_filepath str, full;
	struct dirent* entry;
	DIR* dir;
	char* src;
	int len, is_dir;

	Platform_EncodePath(&str, dirPath);
	dir = opendir(str.buffer);
	if (!dir) return errno;

	String_InitArray(path, pathBuffer);
	while ((entry = readdir(dir))) {
		path.length = 0;
		String_Format1(&path, "%s/", dirPath);

		src = entry->d_name;
		if (src[0] == '.' && src[1] == '\0') continue;
		if (src[0] == '.' && src[1] == '.' && src[2] == '\0') continue;

		len = String_Length(src);
		String_AppendUtf8(&path, src, len);

		if (entry->d_type == DT_DIR)      is_dir = 1;
		else if (entry->d_type == DT_REG) is_dir = 0;
		else { Platform_EncodePath(&full, &path); is_dir = IsDirectory(full.buffer); }
		callback(&path, obj, is_dir);
	}
	closedir(dir);
	return 0;
}

static cc_result File_Do(cc_file* file, const char* path, int mode) {
	*file = open(path, mode | O_CLOEXEC, 0644);
	return *file == -1 ? errno : 0;
}

cc_result File_Open(cc_file* file, const cc_filepath* path) {
	return File_Do(file, path->buffer, O_RDONLY);
}
cc_result File_Create(cc_file* file, const cc_filepath* path) {
	return File_Do(file, path->buffer, O_RDWR | O_CREAT | O_TRUNC);
}
cc_result File_OpenOrCreate(cc_file* file, const cc_filepath* path) {
	return File_Do(file, path->buffer, O_RDWR | O_CREAT);
}

cc_result File_Read(cc_file file, void* data, cc_uint32 count, cc_uint32* bytesRead) {
	int n = (int)read(file, data, count);
	*bytesRead = n < 0 ? 0 : (cc_uint32)n;
	return n < 0 ? errno : 0;
}

cc_result File_Write(cc_file file, const void* data, cc_uint32 count, cc_uint32* bytesWrote) {
	int n = (int)write(file, data, count);
	*bytesWrote = n < 0 ? 0 : (cc_uint32)n;
	return n < 0 ? errno : 0;
}

cc_result File_Close(cc_file file) {
	return close(file) == -1 ? errno : 0;
}

cc_result File_Seek(cc_file file, int offset, int seekType) {
	static const cc_uint8 modes[3] = { SEEK_SET, SEEK_CUR, SEEK_END };
	return lseek(file, offset, modes[seekType]) == -1 ? errno : 0;
}

cc_result File_Position(cc_file file, cc_uint32* pos) {
	off_t at = lseek(file, 0, SEEK_CUR);
	*pos = (cc_uint32)at;
	return at == -1 ? errno : 0;
}

cc_result File_Length(cc_file file, cc_uint32* len) {
	off_t at = lseek(file, 0, SEEK_CUR), end;
	if (at == -1) { *len = -1; return errno; }
	end = lseek(file, 0, SEEK_END);
	lseek(file, at, SEEK_SET);
	if (end == -1) { *len = -1; return errno; }
	*len = (cc_uint32)end;
	return 0;
}


/*########################################################################################################################*
*--------------------------------------------------------Threading--------------------------------------------------------*
*#########################################################################################################################*/
/* Single threaded (CC_BUILD_COOPTHREADED, no sound) */
void Thread_Sleep(cc_uint32 milliseconds) {
	usleep(milliseconds * 1000);
}


/*########################################################################################################################*
*--------------------------------------------------------Platform---------------------------------------------------------*
*#########################################################################################################################*/
cc_bool Q2_FastLeaves; /* q2-fastleaves: leaves as solid blocks (Block.c) */

/* ClassiCube's own textures (classicube/misc/cc_textures.zip, the card's texpacks/default.zip), built
 * in: used when the card's copy is missing or can't be read (TexturePack.c), so a copy to the card
 * that leaves it out doesn't leave the game without textures */
__asm__(".section .rodata.q2_textures,\"a\"\n"
		".balign 4\n"
		".global q2_textures\n"
		"q2_textures:\n"
		".incbin \"classicube/misc/cc_textures.zip\"\n"
		".global q2_textures_end\n"
		"q2_textures_end:\n"
		".previous\n");
cc_bool Q2_Occlusion = true; /* q2-occlusion: chunks hidden behind others skipped (Graphics_SoftGPU.c) */

static volatile sig_atomic_t quit_asked;
static cc_uint32 last_input;

static void OnTerm(int sig) { quit_asked = 1; }

cc_bool Q2_QuitAsked(void) { return quit_asked != 0; }
void Q2_NoteInput(void)    { last_input = NowMS(); }

int Q2_ParseInt(const char* s, const char** end) {
	int sign = 1, value = 0;
	while (*s == ' ') s++;
	if (*s == '-') { sign = -1; s++; }
	else if (*s == '+') { s++; }
	while (*s >= '0' && *s <= '9') { value = value * 10 + (*s - '0'); s++; }
	if (end) *end = s;
	return sign * value;
}

void Platform_Init(void) { }
void Platform_Free(void) { }

cc_bool Platform_DescribeError(cc_result res, cc_string* dst) {
	const char* str;
	if (res >= 1000) return false;
	str = strerror(res);
	if (!str) return false;
	String_AppendUtf8(dst, str, String_Length(str));
	return true;
}

void Process_Exit(cc_result code) {
	Q2_FbClose();
	Q2_InputClose();
	exit(code);
}

cc_result Process_StartGame2(const cc_string* args, int numArgs) {
	return SetGameArgs(args, numArgs);
}

int Platform_GetCommandLineArgs(int argc, STRING_REF char** argv, cc_string* args) {
	return GetGameArgs(args);
}

/* main() has already moved to the app's folder */
cc_result Platform_SetDefaultCurrentDirectory(void) { return 0; }

void CPU_FlushDataCache(void* start, int length) { }
void CPU_InvalidateDataCache(void* start, int length) { }


/*########################################################################################################################*
*---------------------------------------------------------The world-------------------------------------------------------*
*#########################################################################################################################*/
/* Writes the world to WORLD_TEMP, then moves it over WORLD_FILE, so an interrupted save (the
 * game KILLed, the card pulled) leaves the last good world. */
static void SaveWorld(void) {
	static const cc_string temp = String_FromConst(WORLD_TEMP);
	struct Stream stream, compStream;
	struct GZipState* state;
	cc_filepath raw_path;
	cc_result res;
	cc_uint32 beg = NowMS(), took;

	if (!Server.IsSinglePlayer || !World.Loaded || !World.Blocks) return;
	state = (struct GZipState*)Mem_TryAlloc(1, sizeof(struct GZipState));
	if (!state) { Platform_LogConst("Save: out of memory"); return; }

	mkdir("maps", 0777);
	Platform_EncodePath(&raw_path, &temp);
	res = Stream_CreatePath(&stream, &raw_path);
	if (!res) {
		GZip_MakeStream(&compStream, state, &stream);
		res = Cw_Save(&compStream);
		if (!res) res = compStream.Close(&compStream);
		if (res) stream.Close(&stream);
		else     res = stream.Close(&stream);
	}
	Mem_Free(state);

	if (res) { Platform_Log1("Save failed: error %h", &res); unlink(WORLD_TEMP); return; }
	if (rename(WORLD_TEMP, WORLD_FILE)) { Platform_LogConst("Save: couldn't replace the world"); return; }
	World.LastSave = Game.Time;

	took = NowMS() - beg;
	Platform_Log1("World saved in %i ms", &took);
}

/* Q2 settings for a first start: a short view distance for the software renderer */
/* Each set of defaults is applied once (a flag in options.txt), so a later change in the menus stays;
 * a later set is applied once to an older options.txt too */
static void DefaultOptions(void) {
	if (!Options_GetBool("q2-defaults", false)) {
		Options_SetInt(OPT_VIEW_DISTANCE, 32);
		Options_SetBool(OPT_SMOOTH_LIGHTING, false);
		Options_SetBool("q2-defaults", true);
	}
	/* 1.8: the whole of a 128-block world in view (occlusion culling skips what's hidden), no FPS line */
	if (!Options_GetBool("q2-defaults2", false)) {
		Options_SetInt(OPT_VIEW_DISTANCE, 128);
		Options_SetBool(OPT_SHOW_FPS, false);
		Options_SetBool("q2-defaults2", true);
	}
	/* 1.9: view distance 80 */
	if (!Options_GetBool("q2-defaults3", false)) {
		Options_SetInt(OPT_VIEW_DISTANCE, 80);
		Options_SetBool("q2-defaults3", true);
	}
	/* 1.12: view distance 45 */
	if (!Options_GetBool("q2-defaults4", false)) {
		Options_SetInt(OPT_VIEW_DISTANCE, 45);
		Options_SetBool("q2-defaults4", true);
	}
	/* 1.22: view distance 64, with 1.17 to 1.21's speed (35 to 52 frames a second at 45 on the Q2) */
	if (!Options_GetBool("q2-defaults5", false)) {
		Options_SetInt(OPT_VIEW_DISTANCE, 64);
		Options_SetBool("q2-defaults5", true);
	}
}


/* In-game warnings go to the chat, which is hard to read on the Q2: the log gets them too */
static Logger_DoWarn game_warn;
static void LogWarn(const cc_string* msg) {
	Platform_Log(msg->buffer, msg->length);
	if (game_warn) game_warn(msg);
}


/*########################################################################################################################*
*-----------------------------------------------------Main entrypoint-----------------------------------------------------*
*#########################################################################################################################*/
#include "main_impl.h"

/* The app's folder: where the binary is (Q2 Pod runs it by its full path), else Q2_APP_DIR */
static void EnterAppDir(const char* argv0) {
	char dir[256];
	int i, slash = -1;
	for (i = 0; argv0 && argv0[i] && i < (int)sizeof(dir) - 1; i++) {
		dir[i] = argv0[i];
		if (dir[i] == '/') slash = i;
	}
	if (slash > 0) dir[slash] = '\0';
	if (slash <= 0 || chdir(dir)) { if (chdir(Q2_APP_DIR)) return; }
}

int main(int argc, char** argv) {
	static const cc_string world = String_FromConst(WORLD_FILE);
	cc_filepath world_path;
	cc_uint32 now, last_save, last_memory;
	int fd;

	signal(SIGTERM, OnTerm);
	signal(SIGINT,  OnTerm);
	signal(SIGHUP,  OnTerm);
	signal(SIGPIPE, SIG_IGN);

	EnterAppDir(argc > 0 ? argv[0] : NULL);
	rename(LOG_FILE, LOG_PREVIOUS);
	fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd >= 0) log_fd = fd;
	CatchCrashes();
#ifndef Q2_OPT
#define Q2_OPT "-O3"
#endif
	Platform_LogConst("q2-classicube " Q2_VERSION ", " GAME_APP_TITLE ", built " Q2_OPT);
	LogKernelNotes();
	BeatsFromLastRun();
	LogMemory("at the start");
	LogHealth();
	/* q2app's audio=1: Q2 Pod passes the sound output (and off the jack a volume), for the
	 * sounds (Audio_Q2.c), with Play/Pause, Previous and Next */
	if (argc > 1) Platform_Log2("Sound output %c, volume %c", argv[1], argc > 2 ? argv[2] : "(the DAC's)");
	Q2_SoundDevice = argc > 1 ? argv[1] : NULL;
	Q2_SoundVolume = argc > 2 ? argv[2] : NULL;

	if (!Q2_FbOpen()) { Q2_FbLog(); return 1; }

	SetupProgram(argc, argv);
	DefaultOptions();
	Q2_FastLeaves = Options_GetBool("q2-fastleaves", true);
	Q2_Occlusion  = Options_GetBool("q2-occlusion",  true);
	Options_Get(LOPT_USERNAME, &Game_Username, DEFAULT_USERNAME);

	Platform_EncodePath(&world_path, &world);
	if (File_Exists(&world_path)) {
		String_Copy(&SP_AutoloadMap, &world);
		Platform_LogConst("Loading " WORLD_FILE);
	}

	Game_Setup();
	game_warn = Logger_WarnFunc;
	Logger_WarnFunc = LogWarn;
	/* Q2 Pod passes the sound output's arguments only with q2app's audio=1, and only then sends
	 * Play/Pause, Previous and Next: without them those buttons stay the music player's */
	if (argc <= 1) {
		Chat_AddRaw("&eThe Play/Pause and side buttons control your music:");
		Chat_AddRaw("&e.classicube/q2app needs the line audio=1 for the game");
		Platform_LogConst("No sound output arguments: q2app has no audio=1, so Play/Pause, Previous and Next aren't the game's");
	}
	Platform_Log3("World %i x %i x %i blocks", &World.Width, &World.Height, &World.Length);
	{
		pthread_t t;
		if (pthread_create(&t, NULL, StallWatch, NULL)) Platform_LogConst("No stall watcher: no thread");
	}
	LogMemory("the world loaded");
	last_save = last_input = last_memory = NowMS();
	while (Game_Running) {
		Q2_Phase = "drawing";
		Game_RenderFrame();
		Q2_Frames++;
		Q2_Phase = "between frames";
		now = NowMS();

		if (now - last_input > IDLE_QUIT_MS) {
			Platform_LogConst("No input for 5 minutes: quitting");
			Window_RequestClose();
		}
		if (now - last_memory > MEMORY_LOG_MS) {
			LogSpeed(now - last_memory);
			LogMemory("playing");
			LogHealth();
			last_memory = now;
		}
		if (now - last_save > AUTOSAVE_MS) {
			Q2_Phase = "saving the world";
			SaveWorld();
			last_save = NowMS();
		}
	}

	SaveWorld();
	Game_Free();
	Window_Destroy();
	Window_Free();
	Q2_FbClose();
	unlink(BEAT_FILE);
	Platform_LogConst("Ended");
	return 0;
}
