/* ClassiCube on the Shanling Q2 under Q2 Pod: the window, as a Q2 Pod card game.
 *
 * Q2 Pod (github.com/DiamondBond/q2-pod, docs/internals.md "Card games") forks .classicube/q2-classicube
 * from Library -> Games when the folder's q2app holds game=1. While it runs, Q2 Pod stops painting
 * and the game owns the screen and input:
 *
 *   screen  /dev/fb0: the portrait panel, 320 x 375, 32 bpp, red at bit 16, two pages (320 x 750
 *           virtual). Q2 Pod's view is landscape, 375 x 320, the panel turned anticlockwise, so a
 *           view pixel (X, Y) is the panel's row X, column 319 - Y. ClassiCube draws a 375 x 320
 *           frame (the software renderer), which goes to the hidden page, then FBIOPAN_DISPLAY.
 *   input   text datagrams on /tmp/q2-classicube.input.sock: "k d CODE" / "k u CODE" (AWTK key
 *           codes: Return 170, Centre 218), "t d|m|u X Y" (one finger, in the view's pixels) and
 *           "w N" (the wheel's travel, 200 a turn, rising anticlockwise). Return held 700 ms is
 *           Q2 Pod's: it TERMs the game. q2app has audio=1, so Q2 Pod stops the music and hands
 *           over Play/Pause (171) and the side buttons, Previous (222) and Next (223), too.
 *
 * Controls:
 *   touchscreen  ClassiCube's touch controls: the stick to walk, drag elsewhere to look, a tap
 *                places a block, a hold breaks one, on-screen buttons for jump and the rest
 *   wheel        turn (in a menu or the inventory: scroll; on the on-screen keyboard or numpad:
 *                the next or previous key)
 *   Centre       jump, fly up; pressed twice quickly: fly on or off (in a menu: Enter; on the
 *                keyboard: press the selected key)
 *   Play/Pause   fly down (in a menu: Enter)
 *   Previous     the hotbar slot to the left (in a menu: back)
 *   Next         the hotbar slot to the right
 *   Return       pause menu / back (on the keyboard: close it); hold to leave
 */
#include "Window.h"
#include "Platform.h"
#include "Input.h"
#include "InputHandler.h"
#include "Event.h"
#include "Graphics.h"
#include "String_.h"
#include "Funcs.h"
#include "Bitmap.h"
#include "Errors.h"
#include "ExtMath.h"
#include "Logger.h"
#include "Options.h"
#include "Gui.h"
#include "Inventory.h"
#include "VirtualKeyboard.h"
#include "VirtualDialog.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

#include "q2.h"

#define VIEW_W 375 /* Q2 Pod's landscape view */
#define VIEW_H 320
#define FB_W   320 /* the portrait panel: the view turned */
#define FB_H   375

/* The glass's corners are rounded with an 80 px radius: 24 px in from both edges clears the curve
 * at 45 degrees, so the touch buttons and the chat stay whole. */
#define SAFE_INSET 24

/* AWTK key codes as Q2 Pod sends them */
#define Q2K_RETURN 170
#define Q2K_PLAY   171
#define Q2K_CENTRE 218
#define Q2K_PREV   222
#define Q2K_NEXT   223

/* Wheel travel to camera movement: a raw unit is 8 mouse units, so with the default sensitivity
 * (30) a whole turn of the wheel turns you about 180 degrees. Sensitivity in the options scales it. */
#define WHEEL_LOOK   8
/* Raw units a menu scroll step, Q2 Pod's default wheel tick */
#define WHEEL_TICK  28

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602
#define FBIOPAN_DISPLAY     0x4606

struct _DisplayData DisplayInfo;
struct cc_window WindowInfo;

/*########################################################################################################################*
*--------------------------------------------------------Screen-----------------------------------------------------------*
*#########################################################################################################################*/
#define FBIO_WAITFORVSYNC 0x80044620 /* _IOW('F', 0x20, __u32) in MIPS's numbering (write: bit 31) */
#define PAN_GAP_US_GUESS  17000      /* no refresh measured: about 60 Hz */

static struct {
	int fd, open;
	int vsync;                 /* FBIO_WAITFORVSYNC works */
	cc_uint32 gap;             /* microseconds at least between two swaps: a refresh and a bit */
	cc_uint64 last_pan;
	int pace;                  /* q2-pace (true): off only for the host benchmark (test/bench.sh) */
	unsigned var[40], fix[17]; /* fb_var_screeninfo, fb_fix_screeninfo */
	unsigned char* mem;
	unsigned line;             /* bytes a panel row */
	unsigned y0, back;         /* Q2 Pod's page when we started, the page we draw next */
} fb = { -1 };

/* The page swaps are paced to the panel: at most one a refresh. demo pans and then waits for the
 * refresh (FBIO_WAITFORVSYNC); the game swapped as fast as it drew, and since occlusion culling
 * drew faster, in places faster than the panel refreshes; the whole Q2 then froze on the game's last
 * frame (the main thread stuck in the screen's driver is the guess, which the stall watcher in
 * Platform_Q2.c can confirm). The refresh is timed at the start: six waits. */
static void MeasureRefresh(void) {
	cc_uint32 zero = 0;
	cc_uint64 beg = 0, end;
	int i, us;
	for (i = 0; i < 6; i++)
	{
		if (ioctl(fb.fd, FBIO_WAITFORVSYNC, &zero)) break;
		if (i == 1) beg = Stopwatch_Measure();
	}
	if (i < 6) {
		int err = errno;
		fb.vsync = 0;
		fb.gap   = PAN_GAP_US_GUESS;
		Platform_Log1("Screen: no refresh wait (FBIO_WAITFORVSYNC, errno %i): swaps paced by the clock", &err);
		return;
	}
	end = Stopwatch_Measure();
	us  = (int)(Stopwatch_ElapsedMicroseconds(beg, end) / 4);
	if (us < 5000 || us > 50000) us = PAN_GAP_US_GUESS;
	fb.vsync = 1;
	fb.gap   = us + us / 20;
	{
		int hz10 = 10000000 / us;
		int whole = hz10 / 10, tenth = hz10 % 10;
		Platform_Log3("Screen: refreshes every %i us (%i.%i Hz); one page swap a refresh at most", &us, &whole, &tenth);
	}
}

/* Before a swap: if the last was less than a refresh ago, wait for the next refresh */
static void PaceSwap(void) {
	cc_uint32 zero = 0;
	cc_uint64 now = Stopwatch_Measure();
	cc_uint64 since = Stopwatch_ElapsedMicroseconds(fb.last_pan, now);
	if (!fb.pace) { fb.pace = Options_GetBool("q2-pace", true) ? 1 : -1; }
	if (fb.pace < 0 || since >= fb.gap) return;

	Q2_Phase = "waiting for the screen's refresh";
	if (fb.vsync && !ioctl(fb.fd, FBIO_WAITFORVSYNC, &zero)) return;
	usleep((unsigned)(fb.gap - since));
}

int Q2_FbOpen(void) {
	fb.fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
	if (fb.fd < 0 || ioctl(fb.fd, FBIOGET_VSCREENINFO, fb.var) || ioctl(fb.fd, FBIOGET_FSCREENINFO, fb.fix))
		return 0;
	fb.line = fb.fix[11];
	/* the Q2's panel, or nothing: xres, yres, yres_virtual, bpp, red's offset */
	if (fb.var[0] != FB_W || fb.var[1] != FB_H || fb.var[3] < 2 * FB_H || fb.var[6] != 32 ||
		fb.var[8] != 16 || fb.line < FB_W * 4 || 2 * FB_H * fb.line > fb.fix[5])
		return 0;

	fb.mem = mmap(0, fb.fix[5], PROT_READ | PROT_WRITE, MAP_SHARED, fb.fd, 0);
	if (fb.mem == MAP_FAILED) return 0;
	fb.y0   = fb.var[5];
	fb.back = fb.y0 ? 0 : FB_H;
	memset(fb.mem, 0, 2 * FB_H * fb.line);
	fb.open = 1;
	MeasureRefresh();
	fb.last_pan = Stopwatch_Measure();
	return 1;
}

/* For the log when the panel isn't the Q2's */
void Q2_FbLog(void) {
	int err = errno;
	Platform_Log4("No Q2 framebuffer: fd %i, %i x %i, %i bpp", &fb.fd, &fb.var[0], &fb.var[1], &fb.var[6]);
	Platform_Log3("  virtual height %i, red at %i, errno %i", &fb.var[3], &fb.var[8], &err);
}

/* Puts the panel back on Q2 Pod's page, which repaints the whole screen once we have gone */
void Q2_FbClose(void) {
	if (!fb.open) return;
	if (fb.var[5] != fb.y0) { fb.var[5] = fb.y0; ioctl(fb.fd, FBIOPAN_DISPLAY, fb.var); }
	fb.open = 0;
}

/* A view-sized frame (w x h, stride px a row, red at bit 16) onto a portrait page: view (X, Y) is
 * panel row X, column 319 - Y. A frame smaller than the view is centred in it. */
void Q2_Blit(unsigned char* page, unsigned line, const BitmapCol* src, int w, int h, int stride) {
	int x0 = (VIEW_W - w) / 2, y0 = (VIEW_H - h) / 2;
	int x, y;

	/* along each panel row in address order (the view's column from the bottom up): the screen's
	 * memory is written as it's laid out, the frame (cached) read down its columns */
	for (x = 0; x < w; x++) {
		cc_uint32* out = (cc_uint32*)(page + (unsigned)(x0 + x) * line) + (FB_W - 1 - y0);
		const BitmapCol* in = src + x;
		for (y = h - 1; y >= 0; y--) out[-y] = in[y * stride] | 0xFF000000u;
	}
}

static void FbSwap(void);
static void FbShow(const BitmapCol* src, int w, int h, int stride) {
	if (!fb.open) return;
	Q2_Phase = "copying the frame to the screen";
	Q2_Blit(fb.mem + fb.back * fb.line, fb.line, src, w, h, stride);
	FbSwap();
}

static void FbSwap(void) {
	PaceSwap();
	Q2_Phase = "swapping the screen's pages (FBIOPAN_DISPLAY)";
	fb.var[5] = fb.back;
	ioctl(fb.fd, FBIOPAN_DISPLAY, fb.var);
	fb.last_pan = Stopwatch_Measure();
	fb.back = fb.back ? 0 : FB_H;
	Q2_Phase = "after the swap";
}


/*########################################################################################################################*
*--------------------------------------------------------Window-----------------------------------------------------------*
*#########################################################################################################################*/
static int sock = -1;

/* Q2 Pod sends each input event as a datagram without waiting (MSG_DONTWAIT), and the kernel holds
 * only a few (net.unix.max_dgram_qlen, 10) for a reader that hasn't taken them: read only between
 * frames, a slow frame (a save, chunks building) let touch moves and wheel ticks fill that, and
 * the next were dropped, sometimes a touch's release: the thumbstick then held its last direction
 * (walking on) until the next tap. So a thread takes each datagram as it comes (a blocking recv)
 * into this ring, and the main thread handles them between frames as before. One writer, one
 * reader: the writer's index is published after the message is in place. */
#define RING_N   512 /* events: seconds' worth */
#define RING_MSG 32
static char ring[RING_N][RING_MSG];
static unsigned ring_head, ring_tail; /* the thread's next slot; the main thread's next to read */
static int ring_thread;               /* the thread runs (else ProcessEvents reads the socket) */

static cc_uint32 NowMs(void) { return (cc_uint32)(Stopwatch_Measure() / 1000); }

static void* InputThread(void* arg) {
	char m[RING_MSG];
	int n;
	for (;;) {
		unsigned head, tail;
		n = (int)recv(sock, m, sizeof(m) - 1, 0);
		if (n < 0 && errno == EINTR) continue;
		if (n < 0) break;
		if (n == 0) continue;
		m[n] = '\0';

		head = __atomic_load_n(&ring_head, __ATOMIC_RELAXED);
		tail = __atomic_load_n(&ring_tail, __ATOMIC_ACQUIRE);
		if (head - tail >= RING_N) {
			/* full (the main thread stopped): a move or a wheel tick is dropped, a release or a
			 * press takes the newest slot, so a touch or a key isn't left held */
			if (m[0] == 'w' || (m[0] == 't' && m[2] == 'm')) continue;
			head--;
		}
		memcpy(ring[head % RING_N], m, n + 1);
		__atomic_store_n(&ring_head, head + 1, __ATOMIC_RELEASE);
	}
	return NULL;
}

static void InputOpen(void) {
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	memcpy(addr.sun_path, Q2_INPUT_SOCK, sizeof(Q2_INPUT_SOCK));

	sock = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	unlink(Q2_INPUT_SOCK);
	if (sock >= 0 && bind(sock, (const void*)&addr, sizeof(addr))) {
		Platform_LogConst("Couldn't bind the input socket");
	}
	if (sock >= 0) {
		pthread_t t;
		ring_thread = !pthread_create(&t, NULL, InputThread, NULL);
		if (!ring_thread) Platform_LogConst("No input thread: input read between frames");
	}
}

void Q2_InputClose(void) {
	if (sock < 0) return;
	/* with the thread waiting in recv the socket stays open (the process ends next) */
	if (!ring_thread) { close(sock); sock = -1; }
	unlink(Q2_INPUT_SOCK);
}

static void TouchDevOpen(void);

void Window_PreInit(void) {
	DisplayInfo.CursorVisible = true;
}

void Window_Init(void) {
	DisplayInfo.Width  = VIEW_W;
	DisplayInfo.Height = VIEW_H;
	/* The size of menus, buttons and text (q2-uiscale in options.txt). The view is about the
	 * N64's 320 x 240 port in pixels, whose scale is 0.5. Q2_UI_SCALE is as large as fits: the
	 * two-column menus (Pause, Options) are then 360 px of the view's 375. */
	DisplayInfo.ScaleX = Options_GetFloat(Q2_OPT_UI_SCALE, 0.25f, 2.0f, Q2_UI_SCALE);
	DisplayInfo.ScaleY = DisplayInfo.ScaleX;

	Window_Main.Width   = VIEW_W;
	Window_Main.Height  = VIEW_H;
	Window_Main.Focused = true;
	Window_Main.Exists  = true;
	Window_Main.UIScaleX = DEFAULT_UI_SCALE_X;
	Window_Main.UIScaleY = DEFAULT_UI_SCALE_Y;

	DisplayInfo.ContentOffsetX = Option_GetOffsetX(SAFE_INSET);
	DisplayInfo.ContentOffsetY = Option_GetOffsetY(SAFE_INSET);
	Window_Main.SoftKeyboard   = SOFT_KEYBOARD_VIRTUAL;

	Input_SetTouchMode(true);
	Gui_SetTouchUI(true);
	InputOpen();
	TouchDevOpen();
}

void Window_Free(void) { Q2_InputClose(); }

void Window_Create2D(int width, int height) { Window_Main.Is3D = false; }
void Window_Create3D(int width, int height) { Window_Main.Is3D = true;  }
void Window_Destroy(void) { }

void Window_SetTitle(const cc_string* title) { }
void Clipboard_GetText(cc_string* value) { }
void Clipboard_SetText(const cc_string* value) { }

int Window_GetWindowState(void) { return WINDOW_STATE_FULLSCREEN; }
cc_result Window_EnterFullscreen(void) { return 0; }
cc_result Window_ExitFullscreen(void)  { return 0; }
int Window_IsObscured(void)            { return 0; }

void Window_Show(void) { }
void Window_SetSize(int width, int height) { }
void Window_LockLandscapeOrientation(cc_bool lock) { }

void Window_RequestClose(void) {
	Event_RaiseVoid(&WindowEvents.Closing);
}


/*########################################################################################################################*
*----------------------------------------------------Input processing-----------------------------------------------------*
*#########################################################################################################################*/
#define SENT_ACTION -1 /* in sentKey: the down was a game action, whose up is GameAction's too */
/* The buttons repeat their key-down while held, first within 258 ms. A down for a key we think is
 * held, longer than this after its last, is a new press whose release we never got. */
#define KEY_STALE_MS 500
/* Two Centre presses this close in the game turn flying on or off, as a double jump does in
 * Minecraft (7 ticks there). Measured between the presses' downs; a held press's repeats aren't
 * presses. */
#define DOUBLE_JUMP_MS 350

static struct {
	int sentKey[256]; /* ClassiCube key sent for each Q2 key's down (or SENT_ACTION), for its up */
	cc_uint64 seenAt[256]; /* when each held key's last down (or repeat) came */
	cc_uint64 jumpAt;      /* when the last Centre press in the game went down, 0 after a toggle */
	int menuTravel;   /* wheel units not yet turned into scroll steps */
} in;

/* Whether a menu, the inventory, chat or the on-screen keyboard has the input: then the wheel
 * scrolls (or steps through the keyboard's keys) and Centre is Enter (presses the selected key) */
static cc_bool InMenu(void) { return Gui.InputGrab != NULL || DisplayInfo.ShowingSoftKeyboard; }

/* The ClassiCube key for a Q2 button at this moment; 0 for none, or for a game action (below) */
int Q2_KeyFor(int code, cc_bool menu) {
	switch (code) {
	case Q2K_CENTRE: return menu ? CCKEY_ENTER : CCKEY_SPACE;
	case Q2K_RETURN: return CCKEY_ESCAPE;
	case Q2K_PLAY:   return menu ? CCKEY_ENTER : 0;
	case Q2K_PREV:   return menu ? CCKEY_ESCAPE : 0;
	}
	return 0;
}

/* Fly down lasts at least this long after a Play/Pause down, so a press whose release comes
 * straight after it (a button that reports a quick pulse) still moves you, and the repeats of a
 * held one keep you going down */
#define FLY_DOWN_MIN_MS 300
static cc_uint64 flyDownUntil; /* 0: not flying down; else when a deferred release is due */
static cc_bool   flyDownHeld;

static void FlyDown(cc_bool on) {
	if (on  && Bind_OnTriggered[BIND_FLY_DOWN]) Bind_OnTriggered[BIND_FLY_DOWN](0, &TouchDevice);
	if (!on && Bind_OnReleased[BIND_FLY_DOWN])  Bind_OnReleased[BIND_FLY_DOWN](0, &TouchDevice);
}

/* Each frame: a release of Play/Pause deferred until its FLY_DOWN_MIN_MS are up */
static void FlyDownPoll(void) {
	if (!flyDownUntil || flyDownHeld) return;
	if (Stopwatch_Measure() < flyDownUntil) return;
	flyDownUntil = 0;
	FlyDown(false);
}

/* Play/Pause, Previous and Next in the game, through ClassiCube's own bindings' handlers (as its
 * touch buttons are), so they work whatever keys the bindings have. 1: the button is one. */
int Q2_GameAction(int code, cc_bool down) {
	int i;
	switch (code) {
	case Q2K_PLAY: /* fly down (and noclip down) while held, FLY_DOWN_MIN_MS at least */
		flyDownHeld = down;
		if (down) {
			flyDownUntil = Stopwatch_Measure() + FLY_DOWN_MIN_MS * 1000;
			FlyDown(true);
		}
		return 1;
	case Q2K_PREV:
	case Q2K_NEXT:
		if (down) {
			/* the touch hotbar's last slot is its ⋯ (the inventory): through the 8 shown */
			int slots = Gui_TouchUI ? INVENTORY_BLOCKS_PER_HOTBAR - 1 : INVENTORY_BLOCKS_PER_HOTBAR;
			i = Inventory.SelectedIndex;
			if (i >= slots) i = code == Q2K_NEXT ? -1 : slots; /* from a hidden slot: to an end */
			i += code == Q2K_NEXT ? 1 : -1;
			Inventory_SetSelectedIndex((i + slots) % slots);
			/* the HUD redraws its hotbar only when marked, as ClassiCube's own input marks
			 * every screen: unmarked, the slot showed only at the next redraw, a press late */
			for (i = 0; i < Gui.ScreensCount; i++) Gui_Screens[i]->dirty = true;
		}
		return 1;
	}
	return 0;
}

/* The on-screen keyboard's Enter key, pressed with Centre. ClassiCube's own Enter key only closes
 * the keyboard, and a further Enter from a button reopens it on the text field it belongs to, so
 * the dialog could only be finished with a tap on OK. Here it closes the keyboard and hands the
 * dialog an Enter as a real keyboard's would: OK in a settings dialog, Send in chat. */
static cc_bool KeyboardEnter(void) {
	int idx, mode;
	if (!DisplayInfo.ShowingSoftKeyboard || !kb) return false;
	idx = VirtualKeyboard_GetSelected();
	if (idx < 0 || KB_GetBehaviour(idx) != KB_B_ENTER) return false;

	OnscreenKeyboard_Close();
	mode = Window_Main.SoftKeyboard;
	Window_Main.SoftKeyboard = SOFT_KEYBOARD_NONE; /* the text field takes Enter as Enter */
	Input_SetPressed(CCKEY_ENTER);
	Input_SetReleased(CCKEY_ENTER);
	Window_Main.SoftKeyboard = mode;
	return true;
}

/* A Centre press in the game: the second within DOUBLE_JUMP_MS of the first toggles flying, through
 * ClassiCube's own Fly binding's handler (the Fly button's), which checks it's allowed */
static void DoubleJump(cc_uint64 now) {
	if (in.jumpAt && Stopwatch_ElapsedMicroseconds(in.jumpAt, now) / 1000 < DOUBLE_JUMP_MS) {
		in.jumpAt = 0; /* a third press starts a new pair */
		if (Bind_OnTriggered[BIND_FLY]) Bind_OnTriggered[BIND_FLY](0, &TouchDevice);
	} else {
		in.jumpAt = now;
	}
}

static void ReleaseKey(int code) {
	if (in.sentKey[code] == SENT_ACTION) Q2_GameAction(code, false);
	else if (in.sentKey[code])           Input_SetReleased(in.sentKey[code]);
	in.sentKey[code] = 0;
}

static void OnKey(char what, int code) {
	int key;
	cc_uint64 now;
	if (code < 0 || code > 255) return;
	/* each button event in the log (not the touches or the wheel), with the time in ms: what
	 * reached the game, and when, which shows how a button reports a hold */
	{ int ms = (int)(Stopwatch_Measure() / 1000 % 1000000); Platform_Log3("Key %r %i at %i", &what, &code, &ms); }

	if (what == 'd') {
		now = Stopwatch_Measure();
		if (in.sentKey[code]) {
			cc_uint64 gap = Stopwatch_ElapsedMicroseconds(in.seenAt[code], now) / 1000;
			in.seenAt[code] = now;
			/* a repeat: Play/Pause's renews its fly down */
			if (gap < KEY_STALE_MS) {
				if (in.sentKey[code] == SENT_ACTION && code == Q2K_PLAY) Q2_GameAction(code, true);
				return;
			}
			ReleaseKey(code); /* its release went missing: this is a new press */
		}
		in.seenAt[code] = now;
		if (code == Q2K_CENTRE && KeyboardEnter()) return;
		if (code == Q2K_CENTRE && !InMenu()) DoubleJump(now);
		key = Q2_KeyFor(code, InMenu());
		if (key) {
			in.sentKey[code] = key;
			Input_SetPressed(key);
		} else if (!InMenu() && Q2_GameAction(code, true)) {
			in.sentKey[code] = SENT_ACTION;
		}
	} else {
		ReleaseKey(code);
	}
}

/* A touch whose release never comes (1.18 and 1.19 guessed, 1.20 reads the screen itself). Walking
 * with the stick and pressing Centre (jumping), or turning the wheel, the stick could stick until a
 * tap. Q2 Pod's internals.md (Input queue) has the cause, in the firmware's touch library (tslib's
 * pthres, in Q2 Pod's own process): its pressure filter keeps one state for the screen and the
 * wheel's ring together, so the ring's lift (Centre sits inside the ring) marks the screen's finger
 * lifted too, and when that finger then really lifts its release is dropped there. 1.18 and 1.19
 * let a touch go after a silence, but a thumb resting on the stick sends nothing for many seconds
 * (the Q2's log: 3.6, 9.3 and 15.4 s), and walking stopped while it was held.
 *
 * The kernel has it right: the touchscreen's own input device (hyn_ts, /dev/input/eventN, found in
 * /proc/bus/input/devices) reports every lift as BTN_TOUCH 0, whatever the library makes of it. A
 * thread reads it alongside Q2 Pod (no grab: Q2 Pod reads it too), and a touch the game still holds
 * TOUCH_GRACE_MS after the screen said no finger is let go where it last was (Q2 Pod's own release,
 * when it comes, comes well within that). Until the device has sent something this does nothing,
 * so a device that can't be read (or is grabbed) leaves touches as before. Q2_TOUCHDEV in the
 * environment names another file to read (the tests' FIFO). */
#define TOUCH_GRACE_MS 300
static struct {
	int down, lastX, lastY; /* a finger is down (as the game has it), and where it was */
} tc;

/* What the touchscreen device itself says (written by TouchDevThread) */
static int ts_live;             /* it has sent something: what follows is known */
static int ts_finger;           /* a finger is on the screen */
static cc_uint32 ts_upMs;       /* when the last finger left it */
static char ts_path[64];

/* The touchscreen's event device, from the kernel's list (N: Name="hyn_ts", then its H: Handlers=
 * line's eventN): 1 found */
static int FindTouchscreen(void) {
	static char text[8192];
	int fd, n = 0, r, k;
	char* p; char* h;
	const char* env = getenv("Q2_TOUCHDEV");
	if (env && env[0]) {
		for (k = 0; env[k] && k < (int)sizeof(ts_path) - 1; k++) ts_path[k] = env[k];
		ts_path[k] = '\0';
		return 1;
	}

	fd = open("/proc/bus/input/devices", O_RDONLY | O_CLOEXEC);
	if (fd < 0) return 0;
	while (n < (int)sizeof(text) - 1 && (r = (int)read(fd, text + n, sizeof(text) - 1 - n)) > 0) n += r;
	close(fd);
	text[n] = '\0';

	p = strstr(text, "Name=\"hyn_ts\"");
	if (!p || !(h = strstr(p, "Handlers=")) || !(h = strstr(h, "event"))) return 0;
	k = 0;
	for (h += 5; *h >= '0' && *h <= '9' && k < 3; h++) k++;
	if (!k) return 0;
	memcpy(ts_path, "/dev/input/event", 16);
	memcpy(ts_path + 16, h - k, k);
	ts_path[16 + k] = '\0';
	return 1;
}

/* struct input_event: a timeval (two longs, 32-bit on the Q2) then type, code, value */
struct Q2InputEvent { long sec, usec; unsigned short type, code; int value; };
#define Q2_EV_KEY      1
#define Q2_EV_ABS      3
#define Q2_BTN_TOUCH   0x14a
#define Q2_ABS_PRESSURE 0x18

static void TouchFinger(int on) {
	if (!on && __atomic_load_n(&ts_finger, __ATOMIC_RELAXED)) __atomic_store_n(&ts_upMs, NowMs(), __ATOMIC_RELAXED);
	__atomic_store_n(&ts_finger, on, __ATOMIC_RELEASE);
	if (!__atomic_load_n(&ts_live, __ATOMIC_RELAXED)) __atomic_store_n(&ts_live, 1, __ATOMIC_RELEASE);
}

static void* TouchDevThread(void* arg) {
	struct Q2InputEvent ev[16];
	int fd, n, i, opened = 0, sawBtn = 0;
	for (;;) {
		fd = open(ts_path, O_RDONLY | O_CLOEXEC); /* a FIFO (the tests) waits here for its writer */
		if (fd < 0) { if (!opened) Platform_LogConst("Touchscreen device: couldn't open it"); return NULL; }
		opened = 1;
		while ((n = (int)read(fd, ev, sizeof(ev))) > 0) {
			for (i = 0; i < n / (int)sizeof(ev[0]); i++)
			{
				/* BTN_TOUCH; the pressure only from a device that never sends it */
				if (ev[i].type == Q2_EV_KEY && ev[i].code == Q2_BTN_TOUCH) { sawBtn = 1; TouchFinger(ev[i].value != 0); }
				if (ev[i].type == Q2_EV_ABS && ev[i].code == Q2_ABS_PRESSURE && !sawBtn) TouchFinger(ev[i].value > 0);
			}
		}
		close(fd);
		if (n < 0 && errno != EINTR) return NULL; /* the device gone */
		/* n == 0: a FIFO's writer closed it: open it again for the next */
	}
	return NULL;
}

static void TouchDevOpen(void) {
	pthread_t t;
	if (!FindTouchscreen()) {
		Platform_LogConst("No touchscreen device (hyn_ts): a lift the firmware loses stays held until a tap");
		return;
	}
	if (pthread_create(&t, NULL, TouchDevThread, NULL)) return;
	Platform_Log1("Touchscreen device %c: read for its lifts", ts_path);
}

static void OnTouch(char what, int x, int y) {
	/* Q2 Pod reports one finger, in view pixels: the window's own */
	Math_Clamp(x, 0, VIEW_W - 1);
	Math_Clamp(y, 0, VIEW_H - 1);

	if (what == 'd') {
		/* a press while one is still down: its release was lost (Q2 Pod's own input queue can
		 * drop one, internals.md), so it's released here first, where it was, not left held */
		if (tc.down) Input_RemoveTouch(0, tc.lastX, tc.lastY);
		Input_AddTouch(0, x, y);
		tc.down = true;
	} else if (what == 'm') {
		if (!tc.down) return; /* a move with no press: the press was lost, and there's nothing to move */
		Input_UpdateTouch(0, x, y);
	} else {
		if (!tc.down) return;
		Input_RemoveTouch(0, x, y);
		tc.down = false;
	}
	tc.lastX = x; tc.lastY = y;
}

/* After the datagrams each frame: a touch the screen itself says ended, whose release hasn't come */
static void TouchLiftCheck(void) {
	int up;
	if (!tc.down || !__atomic_load_n(&ts_live, __ATOMIC_ACQUIRE)) return;
	if (__atomic_load_n(&ts_finger, __ATOMIC_ACQUIRE)) return;
	up = (int)(NowMs() - __atomic_load_n(&ts_upMs, __ATOMIC_RELAXED));
	if (up < TOUCH_GRACE_MS) return;

	Platform_Log1("Touch let go: the screen's lift was %i ms ago and its release never came", &up);
	Input_RemoveTouch(0, tc.lastX, tc.lastY);
	tc.down = false;
}

/* The on-screen keyboard or numpad (a text or number field): one step to the next key in reading
 * order (dir 1) or the one before (-1), wrapping, past the empty cells a wide key covers */
void Q2_KeyboardStep(int dir) {
	int cells, idx;
	if (!DisplayInfo.ShowingSoftKeyboard || !kb || !KB_MarkDirty) return;
	cells = kb->cellsPerRow * kb->numRows;

	/* Nothing selected after a touch's release, though the first key may still be drawn lit (the
	 * keyboard opened by a tap): move on from the first key, so no step is lost */
	idx = VirtualKeyboard_GetSelected();
	if (idx < 0) idx = 0;
	do { idx = (idx + dir + cells) % cells; } while (KB_IsInvisible(idx));
	kb_curX = idx % kb->cellsPerRow;
	kb_curY = idx / kb->cellsPerRow;
	KB_MarkDirty();
}

static void OnWheel(int travel) {
	if (DisplayInfo.ShowingSoftKeyboard) {
		/* clockwise (falling) goes forward through the keys, as reading */
		if ((travel > 0) != (in.menuTravel > 0)) in.menuTravel = 0;
		in.menuTravel += travel;
		while (in.menuTravel >=  WHEEL_TICK) { in.menuTravel -= WHEEL_TICK; Q2_KeyboardStep(-1); }
		while (in.menuTravel <= -WHEEL_TICK) { in.menuTravel += WHEEL_TICK; Q2_KeyboardStep( 1); }
		return;
	}
	if (!InMenu()) {
		in.menuTravel = 0;
		/* anticlockwise (rising) turns left */
		Event_RaiseRawMove(&PointerEvents.RawMoved, (float)(-travel * WHEEL_LOOK), 0.0f);
		return;
	}

	if ((travel > 0) != (in.menuTravel > 0)) in.menuTravel = 0; /* a reversal steps at once */
	in.menuTravel += travel;
	while (in.menuTravel >=  WHEEL_TICK) { in.menuTravel -= WHEEL_TICK; Mouse_ScrollVWheel( 1.0f); }
	while (in.menuTravel <= -WHEEL_TICK) { in.menuTravel += WHEEL_TICK; Mouse_ScrollVWheel(-1.0f); }
}

/* One datagram from Q2 Pod */
void Q2_Input(const char* m) {
	char b;
	int x, y;
	Q2_NoteInput();

	if (m[0] == 'k' && m[1] == ' ' && m[2] && m[3] == ' ') {
		OnKey(m[2], Q2_ParseInt(m + 4, NULL));
	} else if (m[0] == 't' && m[1] == ' ' && m[2] && m[3] == ' ') {
		const char* p = m + 4;
		b = m[2];
		x = Q2_ParseInt(p, &p);
		y = Q2_ParseInt(p, &p);
		OnTouch(b, x, y);
	} else if (m[0] == 'w' && m[1] == ' ') {
		OnWheel(Q2_ParseInt(m + 2, NULL));
	}
}

void Window_ProcessEvents(float delta) {
	char m[64];
	int n;

	if (ring_thread) {
		unsigned tail = ring_tail;
		while (tail != __atomic_load_n(&ring_head, __ATOMIC_ACQUIRE)) {
			Q2_Input(ring[tail % RING_N]);
			tail++;
			__atomic_store_n(&ring_tail, tail, __ATOMIC_RELEASE);
		}
	} else {
		while (sock >= 0 && (n = (int)recv(sock, m, sizeof(m) - 1, MSG_DONTWAIT)) > 0) {
			m[n] = '\0';
			Q2_Input(m);
		}
	}
	TouchLiftCheck();
	FlyDownPoll();
	if (Q2_QuitAsked() && Window_Main.Exists) {
		Window_Main.Exists = false;
		Window_RequestClose();
	}
}

void Cursor_SetPosition(int x, int y) { }
void Window_EnableRawMouse(void)  { Input.RawMode = true;  }
void Window_UpdateRawMouse(void)  { }
void Window_DisableRawMouse(void) { Input.RawMode = false; }


/*########################################################################################################################*
*-------------------------------------------------------Gamepads----------------------------------------------------------*
*#########################################################################################################################*/
void Gamepads_PreInit(void) { }
void Gamepads_Init(void)    { }
void Gamepads_Process(float delta) { }


/*########################################################################################################################*
*------------------------------------------------------Framebuffer--------------------------------------------------------*
*#########################################################################################################################*/
void Window_AllocFramebuffer(struct Bitmap* bmp, int width, int height) {
	bmp->scan0  = (BitmapCol*)Mem_Alloc(width * height, BITMAPCOLOR_SIZE, "window pixels");
	bmp->width  = width;
	bmp->height = height;
}

void Window_DrawFramebuffer(Rect2D r, struct Bitmap* bmp) {
	int w = min(bmp->width,  VIEW_W);
	int h = min(bmp->height, VIEW_H);
	FbShow(bmp->scan0, w, h, bmp->width);
}

void Window_FreeFramebuffer(struct Bitmap* bmp) {
	Mem_Free(bmp->scan0);
}


/*########################################################################################################################*
*------------------------------------------------------Soft keyboard------------------------------------------------------*
*#########################################################################################################################*/
/* Key sizes for the Q2's glass, whose bottom corners are rounded with an 80 px radius. The numpad
 * (7 tiles wide, 4 rows) at 24 px is 168 x 96, centred on the bottom edge clear of the corners
 * and below a settings dialog's number field. The letter keyboard (16 x 5) at 21 px is 336 x 105:
 * lifted 30 px, its bottom row's ends are on the glass. */
#define KB_NUMPAD_TILE 24
#define KB_LETTERS_TILE 21
#define KB_LETTERS_LIFT 30

void OnscreenKeyboard_Open(struct OpenKeyboardArgs* args) {
	VirtualKeyboard_Open(args);
	if (kb == &numpad_layout) {
		kb_tileWidth = kb_tileHeight = KB_NUMPAD_TILE;
	} else {
		kb_tileWidth = kb_tileHeight = KB_LETTERS_TILE;
		kb_yOffset += KB_LETTERS_LIFT;
	}
}

void OnscreenKeyboard_SetText(const cc_string* text) {
	VirtualKeyboard_SetText(text);
}

void OnscreenKeyboard_Close(void) {
	VirtualKeyboard_Close();
}


/*########################################################################################################################*
*-------------------------------------------------------Misc/Other--------------------------------------------------------*
*#########################################################################################################################*/
void Window_ShowDialog(const char* title, const char* msg) {
	VirtualDialog_Show(title, msg, false);
}

cc_result Window_OpenFileDialog(const struct OpenFileDialogArgs* args) {
	return ERR_NOT_SUPPORTED;
}

cc_result Window_SaveFileDialog(const struct SaveFileDialogArgs* args) {
	return ERR_NOT_SUPPORTED;
}
