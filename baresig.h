/*
 * baresig.h -- Portable signal handling
 * =======================================
 *
 *  USAGE
 *    #define BARESIG_IMPLEMENTATION
 *    #include "baresig.h"
 *
 *  DEPENDS ON
 *    barestd.h
 *
 *  PLATFORM
 *    POSIX   -- sigaction, sigprocmask, signalfd (Linux only)
 *    Windows -- SetConsoleCtrlHandler + SEH for structured exceptions
 *
 *  DESIGN
 *    Signal handling is inherently global -- we do not try to hide that.
 *    No allocation in any signal handler (async-signal-safe).
 *    Handlers write to a static volatile flag array; the main loop
 *    polls with sig_received().
 *
 *    For the common pattern of "run until SIGINT/SIGTERM":
 *
 *      sig_catch(SIGINT);
 *      sig_catch(SIGTERM);
 *      while (!sig_received(SIGINT) && !sig_received(SIGTERM)) {
 *          // ... work ...
 *      }
 *
 *    For blocking until any caught signal fires:
 *      sig_wait_any();
 *
 *    sig_ignore(sig)  -- install SIG_IGN (POSIX) or no-op handler (Win).
 *    sig_default(sig) -- restore SIG_DFL.
 *    sig_reset()      -- clear all pending flags.
 *
 *  SIGNALS (POSIX names, mapped on Windows where possible)
 *    SIGINT   SIGTERM   SIGHUP   SIGPIPE   SIGUSR1   SIGUSR2
 *    SIGALRM  SIGCHLD   SIGWINCH
 *    Windows only maps: SIGINT (Ctrl-C), SIGTERM (Ctrl-Break).
 *
 *  CAUTION
 *    Only sig_received() and sig_reset() are safe to call from signal
 *    handlers.  Everything else must be called from the main thread only.
 */

/* Feature test macros -- must appear before any system header. */
#if !defined(_WIN32) && !defined(_WIN64)
#        if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#                undef _POSIX_C_SOURCE
#                define _POSIX_C_SOURCE 200809L
#        endif
#        if defined(__linux__)
#                ifndef _DEFAULT_SOURCE
#                        define _DEFAULT_SOURCE 1
#                endif
#        endif
#endif
#ifndef BARESIG_H
#        define BARESIG_H

#        include <signal.h>

#        include "barestd.h"

/* Maximum signal number we track (generous upper bound). */
#        define SIG_MAX_TRACK 64

/* Global flag array -- 1 if that signal has fired at least once. */
extern volatile sig_atomic_t _sig_flags[SIG_MAX_TRACK];

/* ================================================================
 *  API
 * ================================================================ */

/* Install a catching handler for sig.
   Subsequent calls to sig_received(sig) will return true when it fires. */
void sig_catch(int sig);

/* Install SIG_IGN / no-op handler. */
void sig_ignore(int sig);

/* Restore SIG_DFL / platform default. */
void sig_default(int sig);

/* True if sig has been received since the last sig_reset(sig). */
static inline bool sig_received(int sig) {
        if (sig <= 0 || sig >= SIG_MAX_TRACK) return false;
        return _sig_flags[sig] != 0;
}

/* Clear the pending flag for sig. */
static inline void sig_reset(int sig) {
        if (sig > 0 && sig < SIG_MAX_TRACK) _sig_flags[sig] = 0;
}

/* Clear all pending flags. */
void sig_reset_all(void);

/*
 * Block the calling thread until any caught signal fires.
 * Uses sigsuspend (POSIX) or a Sleep loop (Windows).
 * Returns the signal number, or -1 on error.
 */
int sig_wait_any(void);

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#        ifdef BARESIG_IMPLEMENTATION

/* Global flag storage -- defined here in the implementation unit. */
volatile sig_atomic_t _sig_flags[SIG_MAX_TRACK] = {0};

/* Common handler -- async-signal-safe: only writes a volatile flag. */
static void _sig_handler(int sig) {
        if (sig > 0 && sig < SIG_MAX_TRACK) _sig_flags[sig] = 1;
}

void sig_reset_all(void) {
        int i;
        for (i = 0; i < SIG_MAX_TRACK; i++) _sig_flags[i] = 0;
}

/* ---- POSIX ---------------------------------------------------- */
#                if !defined(_WIN32) && !defined(_WIN64)

#                        include <string.h> /* memset */
#                        include <unistd.h>

void sig_catch(int sig) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = _sig_handler;
        sigemptyset(&sa.sa_mask);
        /* SA_RESTART: restart syscalls interrupted by this signal.
           Avoids EINTR noise in read/write/accept loops. */
        sa.sa_flags = SA_RESTART;
        sigaction(sig, &sa, NULL);
}

void sig_ignore(int sig) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = SIG_IGN;
        sigemptyset(&sa.sa_mask);
        sigaction(sig, &sa, NULL);
}

void sig_default(int sig) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sigaction(sig, &sa, NULL);
}

int sig_wait_any(void) {
        /* Block until any signal arrives.
           We mask no signals, so sigsuspend with current mask just waits.
           Use an empty mask so any signal wakes us up. */
        sigset_t empty;
        sigemptyset(&empty);
        sigsuspend(&empty); /* returns on signal delivery */

        /* Find which one fired */
        int i;
        for (i = 1; i < SIG_MAX_TRACK; i++)
                if (_sig_flags[i]) return i;
        return -1;
}

#                else /* Windows */

/* ---- Windows ------------------------------------------------- */

#                        include <windows.h>

/* Windows only supports a handful of signals natively.
   We map Ctrl-C -> SIGINT and Ctrl-Break -> SIGTERM via
   SetConsoleCtrlHandler. */

static BOOL WINAPI _win_ctrl_handler(DWORD type) {
        switch (type) {
                case CTRL_C_EVENT:
                        _sig_handler(SIGINT);
                        return TRUE;
                case CTRL_BREAK_EVENT:
                        _sig_handler(SIGTERM);
                        return TRUE;
                default:
                        return FALSE;
        }
}

static int _win_ctrl_installed = 0;

static void _win_ensure_ctrl(void) {
        if (!_win_ctrl_installed) {
                SetConsoleCtrlHandler(_win_ctrl_handler, TRUE);
                _win_ctrl_installed = 1;
        }
}

void sig_catch(int sig) {
        _win_ensure_ctrl();
        /* Also install via signal() for SIGINT/SIGFPE etc. */
        signal(sig, _sig_handler);
}

void sig_ignore(int sig) {
        signal(sig, SIG_IGN);
}
void sig_default(int sig) {
        signal(sig, SIG_DFL);
}

int sig_wait_any(void) {
        while (1) {
                int i;
                for (i = 1; i < SIG_MAX_TRACK; i++)
                        if (_sig_flags[i]) return i;
                Sleep(10); /* poll every 10ms */
        }
}

#                endif /* platform */
#        endif         /* BARESIG_IMPLEMENTATION */
#endif                 /* BARESIG_H */
