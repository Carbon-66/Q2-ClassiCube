#ifndef _SIGNAL_H
#define _SIGNAL_H
typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIGHUP 1
#define SIGINT 2
#define SIGPIPE 13
#define SIGTERM 15
/* MIPS numbering: SIGBUS is 10 */
#define SIGILL 4
#define SIGABRT 6
#define SIGFPE 8
#define SIGBUS 10
#define SIGSEGV 11
sighandler_t signal(int, sighandler_t);

/* glibc's MIPS o32 layouts */
#define SA_SIGINFO   0x00000008
#define SA_RESETHAND 0x80000000
typedef struct { unsigned long __val[32]; } sigset_t;
typedef struct {
	int si_signo, si_code, si_errno; /* MIPS: code before errno */
	union { void* _addr; int _pad[29]; } _sifields;
} siginfo_t;
#define si_addr _sifields._addr
struct sigaction {
	unsigned int sa_flags;
	union { sighandler_t _handler; void (*_sigaction)(int, siginfo_t*, void*); } __u;
	sigset_t sa_mask;
	void (*sa_restorer)(void);
	int sa_resv[1];
};
#define sa_handler   __u._handler
#define sa_sigaction __u._sigaction
int sigaction(int, const struct sigaction*, struct sigaction*);
#endif
