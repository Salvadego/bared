/*
 * baresync.h -- Mutex, RWLock, Condvar, Once
 * ============================================
 *
 *  USAGE
 *    #define BARESYNC_IMPLEMENTATION
 *    #include "baresync.h"
 *
 *  DEPENDS ON
 *    barestd.h
 *
 *  PLATFORM
 *    POSIX   -- pthreads
 *    Windows -- CRITICAL_SECTION, SRWLock, CONDITION_VARIABLE
 *
 *  DESIGN
 *    All primitives are plain structs -- embed anywhere, zero overhead.
 *    Zero-init ({0}) is always safe before first use.
 *    Zero allocation.  Errors assert.
 *
 *  EXAMPLE
 *
 *    static Mutex g_mu = {0};
 *    mutex_lock(&g_mu);
 *    // ... critical section ...
 *    mutex_unlock(&g_mu);
 *
 *    static Once g_once = {0};
 *    once_run(&g_once, my_init_fn);
 */

#ifndef BARESYNC_H
#define BARESYNC_H

#include "barestd.h"

#if defined(_WIN32) || defined(_WIN64)
#        define _BS_WIN
#        ifndef WIN32_LEAN_AND_MEAN
#                define WIN32_LEAN_AND_MEAN
#        endif
#        include <windows.h>
#else
#        define _BS_POSIX
#        ifndef _POSIX_C_SOURCE
#                define _POSIX_C_SOURCE 200809L
#        endif
#        include <pthread.h>
#endif

/* ================================================================
 *  Mutex
 * ================================================================ */

#ifdef _BS_WIN
typedef struct {
        CRITICAL_SECTION _cs;
        volatile long    _init;
} Mutex;
#else
typedef struct {
        pthread_mutex_t _m;
} Mutex;
#endif

void mutex_lock(Mutex* m);
void mutex_unlock(Mutex* m);
bool mutex_trylock(Mutex* m); /* true = acquired */
void mutex_destroy(Mutex* m);

/* ================================================================
 *  RWLock -- many readers xor one writer
 * ================================================================ */

#ifdef _BS_WIN
typedef struct {
        SRWLOCK _rw;
} RWLock;
#else
typedef struct {
        pthread_rwlock_t _rw;
} RWLock;
#endif

void rwlock_rlock(RWLock* rw);
void rwlock_runlock(RWLock* rw);
void rwlock_wlock(RWLock* rw);
void rwlock_wunlock(RWLock* rw);
void rwlock_destroy(RWLock* rw);

/* ================================================================
 *  Condvar -- always pairs with a Mutex
 * ================================================================ */

#ifdef _BS_WIN
typedef struct {
        CONDITION_VARIABLE _cv;
} Condvar;
#else
typedef struct {
        pthread_cond_t _cv;
} Condvar;
#endif

/* m must be held on entry; released while sleeping, re-acquired on return. */
void condvar_wait(Condvar* cv, Mutex* m);
void condvar_signal(Condvar* cv);
void condvar_broadcast(Condvar* cv);
void condvar_destroy(Condvar* cv);

/* ================================================================
 *  Once -- run fn exactly once across all threads.
 *  Zero-init is always safe: Once o = {0};
 * ================================================================ */

#ifdef _BS_WIN
typedef struct {
        INIT_ONCE _o;
} Once;
#else
/*
 * pthread_once_t needs PTHREAD_ONCE_INIT (not zero everywhere).
 * We use a volatile done-flag + mutex bootstrap so {0} works.
 * Fast path (done==1) is a single volatile load -- no lock.
 */
typedef struct {
        volatile int    _done;
        pthread_mutex_t _mu;
} Once;
#endif

void once_run(Once* o, void (*fn)(void));

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#ifdef BARESYNC_IMPLEMENTATION

/* ---- POSIX ---------------------------------------------------- */
#        ifdef _BS_POSIX

void mutex_lock(Mutex* m) {
        int r = pthread_mutex_lock(&m->_m);
        assert(r == 0 && "mutex_lock");
        Unused(r);
}
void mutex_unlock(Mutex* m) {
        int r = pthread_mutex_unlock(&m->_m);
        assert(r == 0 && "mutex_unlock");
        Unused(r);
}
bool mutex_trylock(Mutex* m) {
        return pthread_mutex_trylock(&m->_m) == 0;
}
void mutex_destroy(Mutex* m) {
        pthread_mutex_destroy(&m->_m);
}

void rwlock_rlock(RWLock* rw) {
        pthread_rwlock_rdlock(&rw->_rw);
}
void rwlock_runlock(RWLock* rw) {
        pthread_rwlock_unlock(&rw->_rw);
}
void rwlock_wlock(RWLock* rw) {
        pthread_rwlock_wrlock(&rw->_rw);
}
void rwlock_wunlock(RWLock* rw) {
        pthread_rwlock_unlock(&rw->_rw);
}
void rwlock_destroy(RWLock* rw) {
        pthread_rwlock_destroy(&rw->_rw);
}

void condvar_wait(Condvar* cv, Mutex* m) {
        pthread_cond_wait(&cv->_cv, &m->_m);
}
void condvar_signal(Condvar* cv) {
        pthread_cond_signal(&cv->_cv);
}
void condvar_broadcast(Condvar* cv) {
        pthread_cond_broadcast(&cv->_cv);
}
void condvar_destroy(Condvar* cv) {
        pthread_cond_destroy(&cv->_cv);
}

void once_run(Once* o, void (*fn)(void)) {
        if (o->_done) return; /* fast path: no lock */
        pthread_mutex_lock(&o->_mu);
        if (!o->_done) {
                fn();
                o->_done = 1;
        } /* run under lock     */
        pthread_mutex_unlock(&o->_mu);
}

#        endif /* _BS_POSIX */

/* ---- Windows ------------------------------------------------- */
#        ifdef _BS_WIN

/*
 * Mutex lazy-init via interlocked: state machine
 *   0 = uninit  1 = initialising  2 = ready
 * Threads that arrive while _init==1 busy-wait (init is very fast).
 */
void mutex_lock(Mutex* m) {
        if (InterlockedCompareExchange(&m->_init, 1, 0) == 0) {
                InitializeCriticalSection(&m->_cs);
                InterlockedExchange(&m->_init, 2);
        }
        while (m->_init != 2) { /* spin -- init takes <1us */
        }
        EnterCriticalSection(&m->_cs);
}
void mutex_unlock(Mutex* m) {
        LeaveCriticalSection(&m->_cs);
}
bool mutex_trylock(Mutex* m) {
        /* ensure init by calling lock+unlock first time only */
        if (m->_init != 2) {
                mutex_lock(m);
                mutex_unlock(m);
        }
        return TryEnterCriticalSection(&m->_cs) != 0;
}
void mutex_destroy(Mutex* m) {
        if (m->_init == 2) {
                DeleteCriticalSection(&m->_cs);
                m->_init = 0;
        }
}

void rwlock_rlock(RWLock* rw) {
        AcquireSRWLockShared(&rw->_rw);
}
void rwlock_runlock(RWLock* rw) {
        ReleaseSRWLockShared(&rw->_rw);
}
void rwlock_wlock(RWLock* rw) {
        AcquireSRWLockExclusive(&rw->_rw);
}
void rwlock_wunlock(RWLock* rw) {
        ReleaseSRWLockExclusive(&rw->_rw);
}
void rwlock_destroy(RWLock* rw) {
        Unused(rw);
}

void condvar_wait(Condvar* cv, Mutex* m) {
        SleepConditionVariableCS(&cv->_cv, &m->_cs, INFINITE);
}
void condvar_signal(Condvar* cv) {
        WakeConditionVariable(&cv->_cv);
}
void condvar_broadcast(Condvar* cv) {
        WakeAllConditionVariable(&cv->_cv);
}
void condvar_destroy(Condvar* cv) {
        Unused(cv);
}

static BOOL CALLBACK _once_cb(PINIT_ONCE io, PVOID p, PVOID* ctx) {
        Unused(io);
        Unused(ctx);
        ((void (*)(void))p)();
        return TRUE;
}
void once_run(Once* o, void (*fn)(void)) {
        InitOnceExecuteOnce(&o->_o, _once_cb, (PVOID)(uintptr_t)fn, NULL);
}

#        endif /* _BS_WIN */
#endif         /* BARESYNC_IMPLEMENTATION */
#endif         /* BARESYNC_H */
