/*
 * barethreads.h -- Thread spawn, join, detach, TLS
 * ==================================================
 *
 *  USAGE
 *    #define BARETHREADS_IMPLEMENTATION
 *    #include "barethreads.h"
 *
 *  DEPENDS ON
 *    barestd.h, baresync.h
 *
 *  PLATFORM
 *    POSIX   -- pthreads
 *    Windows -- CreateThread / TlsAlloc
 *
 *  DESIGN
 *    Thread    -- opaque handle; spawn gives you one, join/detach consumes it.
 *    TLSKey    -- thread-local slot key; each thread stores its own pointer.
 *    ThreadFn  -- typedef void *(*ThreadFn)(void *arg);
 *
 *    thread_spawn allocates a tiny ThreadSpawnCtx from the passed arena
 *    to ferry (fn, arg) across the OS thread boundary, then frees it
 *    inside the new thread -- so no per-thread arena is needed by the
 *    caller and the cost is one Arena allocation (~32 bytes).
 *
 *    For per-thread arenas the recommended pattern is:
 *
 *      void *my_thread(void *arg) {
 *          Arena *a = arena_new(MB(1));
 *          // ... work ...
 *          arena_free(a);
 *          return NULL;
 *      }
 *
 *  EXAMPLE
 *
 *    // Spawn + join
 *    Arena  *a = arena_new(KB(1));
 *    Thread  t = thread_spawn(a, my_fn, my_arg);
 *    void   *ret;
 *    thread_join(t, &ret);
 *
 *    // Thread-local storage
 *    TLSKey key = tls_alloc();
 *    tls_set(key, my_ptr);
 *    void *p = tls_get(key);
 *    tls_free(key);
 */

/* Feature test macros -- must appear before any system header. */
#if !defined(_WIN32) && !defined(_WIN64)
#  if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#    undef  _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#  if defined(__linux__)
#    ifndef _DEFAULT_SOURCE
#      define _DEFAULT_SOURCE 1
#    endif
#  endif
#endif
#ifndef BARETHREADS_H
#define BARETHREADS_H

#include "barestd.h"
#include "baresync.h"

#if defined(_WIN32) || defined(_WIN64)
#  define _BT_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
typedef HANDLE   _BtHandle;
typedef DWORD    _BtTLS;
#else
#  define _BT_POSIX
#  include <pthread.h>
typedef pthread_t     _BtHandle;
typedef pthread_key_t _BtTLS;
#endif

/* ================================================================
 *  Types
 * ================================================================ */

typedef void *(*ThreadFn)(void *arg);

typedef struct { _BtHandle _h; } Thread;
typedef struct { _BtTLS    _k; } TLSKey;

/* ================================================================
 *  Thread API
 * ================================================================ */

/*
 * Spawn a new thread running fn(arg).
 * arena is used for a small internal ctx struct (~32 B) which the
 * spawned thread frees before calling fn.  Use a scratch arena or
 * a long-lived one -- does not matter, the alloc is immediately freed.
 */
Thread thread_spawn (Arena *a, ThreadFn fn, void *arg);

/*
 * Wait for t to finish.  ret_out receives the thread's return value
 * (may be NULL if you don't care).  Consumes t.
 */
void   thread_join  (Thread t, void **ret_out);

/*
 * Let t run independently; its resources are freed when it exits.
 * Do not call join after detach.  Consumes t.
 */
void   thread_detach(Thread t);

/* Yield the current thread's timeslice. */
void   thread_yield(void);

/* Current thread ID as an opaque integer -- useful for logging. */
uint64_t thread_id(void);

/* ================================================================
 *  TLS -- Thread-Local Storage slots
 * ================================================================ */

TLSKey tls_alloc(void);
void   tls_free (TLSKey k);
void  *tls_get  (TLSKey k);
void   tls_set  (TLSKey k, void *val);

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#ifdef BARETHREADS_IMPLEMENTATION

#include <stdlib.h>

/* Small ctx carried across the thread boundary.
   Allocated with malloc (2 pointers = 16 B) and freed by the new thread.
   We deliberately avoid touching the caller's arena from another thread. */
typedef struct {
        ThreadFn  fn;
        void     *arg;
} _BtCtx;

/* ---- POSIX ---------------------------------------------------- */
#ifdef _BT_POSIX

#include <sched.h>  /* sched_yield */

static void *_bt_trampoline(void *raw) {
        _BtCtx ctx = *(_BtCtx *)raw;
        free(raw);  /* free the tiny heap alloc before calling fn */
        return ctx.fn(ctx.arg);
}

Thread thread_spawn(Arena *a, ThreadFn fn, void *arg) {
        Unused(a);
        _BtCtx *ctx = (_BtCtx *)malloc(sizeof(_BtCtx));
        assert(ctx && "thread_spawn: malloc failed");
        ctx->fn  = fn;
        ctx->arg = arg;

        Thread t;
        int r = pthread_create(&t._h, NULL, _bt_trampoline, ctx);
        assert(r == 0 && "thread_spawn: pthread_create failed");
        Unused(r);
        return t;
}

void thread_join(Thread t, void **ret_out) {
        void *rv = NULL;
        pthread_join(t._h, &rv);
        if (ret_out) *ret_out = rv;
}

void     thread_detach(Thread t)   { pthread_detach(t._h); }
void     thread_yield (void)       { sched_yield(); }
uint64_t thread_id    (void)       { return (uint64_t)(uintptr_t)pthread_self(); }

TLSKey tls_alloc(void) {
        TLSKey k;
        int r = pthread_key_create(&k._k, NULL);
        assert(r == 0 && "tls_alloc");
        Unused(r);
        return k;
}
void  tls_free(TLSKey k) { pthread_key_delete(k._k); }
void *tls_get (TLSKey k) { return pthread_getspecific(k._k); }
void  tls_set (TLSKey k, void *v) { pthread_setspecific(k._k, v); }

#endif /* _BT_POSIX */

/* ---- Windows ------------------------------------------------- */
#ifdef _BT_WIN

static DWORD WINAPI _bt_trampoline(LPVOID raw) {
        _BtCtx ctx = *(_BtCtx *)raw;
        free(raw);
        ctx.fn(ctx.arg);
        return 0;
}

Thread thread_spawn(Arena *a, ThreadFn fn, void *arg) {
        Unused(a);
        _BtCtx *ctx = (_BtCtx *)malloc(sizeof(_BtCtx));
        assert(ctx && "thread_spawn: malloc failed");
        ctx->fn  = fn;
        ctx->arg = arg;

        Thread t;
        t._h = CreateThread(NULL, 0, _bt_trampoline, ctx, 0, NULL);
        assert(t._h != NULL && "thread_spawn: CreateThread failed");
        return t;
}

void thread_join(Thread t, void **ret_out) {
        WaitForSingleObject(t._h, INFINITE);
        if (ret_out) {
                DWORD code = 0;
                GetExitCodeThread(t._h, &code);
                *ret_out = (void *)(uintptr_t)code;
        }
        CloseHandle(t._h);
}

void     thread_detach(Thread t)   { CloseHandle(t._h); }
void     thread_yield (void)       { Sleep(0); }
uint64_t thread_id    (void)       { return (uint64_t)GetCurrentThreadId(); }

TLSKey tls_alloc(void) {
        TLSKey k;
        k._k = TlsAlloc();
        assert(k._k != TLS_OUT_OF_INDEXES && "tls_alloc");
        return k;
}
void  tls_free(TLSKey k) { TlsFree(k._k); }
void *tls_get (TLSKey k) { return TlsGetValue(k._k); }
void  tls_set (TLSKey k, void *v) { TlsSetValue(k._k, v); }

#endif /* _BT_WIN */
#endif /* BARETHREADS_IMPLEMENTATION */
#endif /* BARETHREADS_H */
