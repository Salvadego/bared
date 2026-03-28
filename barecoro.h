/*
 * barecoro.h -- Stackful coroutines (cooperative multitasking)
 * =============================================================
 *
 *  USAGE
 *    #define BARECORO_IMPLEMENTATION
 *    #include "barecoro.h"
 *
 *  DEPENDS ON
 *    barestd.h
 *
 *  PLATFORM
 *    POSIX   -- ucontext_t (Linux, macOS)
 *    Windows -- Fibers (ConvertThreadToFiber / CreateFiber)
 *
 *  DESIGN
 *    A Coro is a stackful coroutine: it has its own full C stack
 *    (default 64 KB, configurable) and can suspend/resume at any call depth.
 *    Unlike async/await, no coloring — any function can yield.
 *
 *    Scheduling model: cooperative, explicit yield.
 *    No implicit preemption. You control when context switches happen.
 *
 *    CoroScheduler: a simple round-robin scheduler over a fixed-size
 *    array of coroutines. Drives the event loop with coro_run_all().
 *
 *    Memory: each coroutine's stack is malloc'd (not arena — stacks must
 *    remain at a stable address). The Coro header itself is arena-allocated.
 *
 *  LIFECYCLE
 *    CORO_READY    -- created, not yet run
 *    CORO_RUNNING  -- currently executing
 *    CORO_SUSPENDED-- yielded, waiting to be resumed
 *    CORO_DONE     -- returned from fn, will not run again
 *
 *  API
 *    Coro *coro_new(a, fn, arg, stack_sz)  -- create coroutine
 *    void  coro_resume(c)                  -- resume until next yield or done
 *    void  coro_yield(c)                   -- yield from inside a coroutine
 *    bool  coro_done(c)                    -- true if coroutine has finished
 *    void  coro_free(c)                    -- free stack (call when done)
 *    void *coro_result(c)                  -- return value after done
 *
 *    CoroScheduler *coro_sched_new(a, cap) -- create scheduler
 *    void coro_sched_add(s, c)             -- add coroutine to scheduler
 *    void coro_run_all(s)                  -- run until all are done
 *    int  coro_active(s)                   -- count of non-done coroutines
 *
 *  EXAMPLE -- generator
 *
 *    static void counter(Coro *self) {
 *        int *n = (int*)self->arg;
 *        while (*n < 5) {
 *            printf("count: %d\n", (*n)++);
 *            coro_yield(self);
 *        }
 *    }
 *
 *    Arena *a = arena_new(KB(4));
 *    int n = 0;
 *    Coro *c = coro_new(a, counter, &n, KB(64));
 *    while (!coro_done(c)) coro_resume(c);
 *    coro_free(c);
 *
 *  EXAMPLE -- scheduler (multiple concurrent tasks)
 *
 *    CoroScheduler *s = coro_sched_new(a, 16);
 *    coro_sched_add(s, coro_new(a, task_a, NULL, KB(64)));
 *    coro_sched_add(s, coro_new(a, task_b, NULL, KB(64)));
 *    coro_run_all(s);   // runs round-robin until all done
 */
#ifndef BARECORO_H
#define BARECORO_H

#include <stdbool.h>
#include <stddef.h>

#include "barestd.h"

/* ================================================================
 *  Platform setup
 * ================================================================ */
#if defined(_WIN32) || defined(_WIN64)
#        define _BC_WIN
#        ifndef WIN32_LEAN_AND_MEAN
#                define WIN32_LEAN_AND_MEAN
#        endif
#        include <windows.h>
#else
#        define _BC_POSIX
#        if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200112L
#                undef _POSIX_C_SOURCE
#                define _POSIX_C_SOURCE 200112L
#        endif
#        include <ucontext.h>
#endif

/* ================================================================
 *  Coro state
 * ================================================================ */
typedef enum {
        CORO_READY     = 0,
        CORO_RUNNING   = 1,
        CORO_SUSPENDED = 2,
        CORO_DONE      = 3,
} CoroState;

typedef struct Coro Coro;
typedef void (*CoroFn)(Coro* self);

struct Coro {
        CoroState state;
        CoroFn    fn;
        void*     arg;    /* caller-supplied argument; readable as self->arg */
        void*     result; /* set before returning; readable via coro_result() */

#ifdef _BC_POSIX
        ucontext_t ctx;    /* coroutine context            */
        ucontext_t caller; /* saved caller context         */
#else
        LPVOID fiber;        /* coroutine fiber              */
        LPVOID caller_fiber; /* caller fiber             */
#endif

        void*  stack; /* malloc'd stack memory        */
        size_t stack_sz;
};

/* ================================================================
 *  Coroutine API
 * ================================================================ */

/* Create a new coroutine. stack_sz defaults to KB(64) if 0. */
Coro* coro_new(Arena* a, CoroFn fn, void* arg, size_t stack_sz);

/* Resume coroutine until it yields or returns. */
void coro_resume(Coro* c);

/* Yield from inside a coroutine back to its caller. */
void coro_yield(Coro* c);

/* True if the coroutine has returned. */
static inline bool coro_done(const Coro* c) {
        return c->state == CORO_DONE;
}

/* Free the coroutine's stack. Safe to call only after coro_done(). */
void coro_free(Coro* c);

/* Return value set by the coroutine before it returned. NULL if not set. */
static inline void* coro_result(const Coro* c) {
        return c->result;
}

/* ================================================================
 *  Scheduler — cooperative round-robin
 * ================================================================ */
#define CORO_SCHED_MAX 256

typedef struct {
        Coro* coros[CORO_SCHED_MAX];
        int   count;
        int   cap;
} CoroScheduler;

CoroScheduler* coro_sched_new(Arena* a, int cap);
void           coro_sched_add(CoroScheduler* s, Coro* c);
void           coro_run_all(CoroScheduler* s);
int            coro_active(CoroScheduler* s);

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#ifdef BARECORO_IMPLEMENTATION
#        include <stdlib.h>
#        include <string.h>

#        ifdef _BC_POSIX

/* The trampoline calls fn(self) then marks the coro done and
   switches back to the caller. */
static void _coro_trampoline(uint32_t hi, uint32_t lo) {
        Coro* c  = (Coro*)(((uintptr_t)hi << 32) | (uintptr_t)lo);
        c->state = CORO_RUNNING;
        c->fn(c);
        c->state = CORO_DONE;
        /* Return to caller — swapcontext back */
        swapcontext(&c->ctx, &c->caller);
}

Coro* coro_new(Arena* a, CoroFn fn, void* arg, size_t stack_sz) {
        if (!stack_sz) stack_sz = KB(64);
        Coro* c = arena_push_type(a, Coro);
        memset(c, 0, sizeof *c);
        c->fn       = fn;
        c->arg      = arg;
        c->state    = CORO_READY;
        c->stack_sz = stack_sz;
        c->stack    = malloc(stack_sz);
        assert(c->stack && "coro_new: malloc failed");

        getcontext(&c->ctx);
        c->ctx.uc_stack.ss_sp   = c->stack;
        c->ctx.uc_stack.ss_size = stack_sz;
        c->ctx.uc_link          = NULL; /* we handle return manually */

        /* Pass pointer as two uint32_t values for portability with makecontext
         */
        uintptr_t ptr = (uintptr_t)c;
        uint32_t  hi  = (uint32_t)(ptr >> 32);
        uint32_t  lo  = (uint32_t)(ptr);
        makecontext(&c->ctx, (void (*)(void))_coro_trampoline, 2, hi, lo);
        return c;
}

void coro_resume(Coro* c) {
        assert(c->state != CORO_DONE && "coro_resume: already done");
        c->state = CORO_RUNNING;
        swapcontext(&c->caller, &c->ctx);
}

void coro_yield(Coro* c) {
        c->state = CORO_SUSPENDED;
        swapcontext(&c->ctx, &c->caller);
}

void coro_free(Coro* c) {
        if (c->stack) {
                free(c->stack);
                c->stack = NULL;
        }
}

#        endif /* _BC_POSIX */

#        ifdef _BC_WIN

typedef struct {
        Coro* c;
} _CoroWinArg;
static VOID CALLBACK _coro_fiber_fn(LPVOID param) {
        Coro* c  = (Coro*)param;
        c->state = CORO_RUNNING;
        c->fn(c);
        c->state = CORO_DONE;
        SwitchToFiber(c->caller_fiber);
}

Coro* coro_new(Arena* a, CoroFn fn, void* arg, size_t stack_sz) {
        if (!stack_sz) stack_sz = KB(64);
        Coro* c = arena_push_type(a, Coro);
        memset(c, 0, sizeof *c);
        c->fn       = fn;
        c->arg      = arg;
        c->state    = CORO_READY;
        c->stack_sz = stack_sz;
        c->fiber    = CreateFiber(stack_sz, _coro_fiber_fn, c);
        assert(c->fiber && "coro_new: CreateFiber failed");
        return c;
}

void coro_resume(Coro* c) {
        assert(c->state != CORO_DONE);
        c->caller_fiber = GetCurrentFiber();
        if (c->caller_fiber == NULL)
                c->caller_fiber = ConvertThreadToFiber(NULL);
        c->state = CORO_RUNNING;
        SwitchToFiber(c->fiber);
}

void coro_yield(Coro* c) {
        c->state = CORO_SUSPENDED;
        SwitchToFiber(c->caller_fiber);
}

void coro_free(Coro* c) {
        if (c->fiber) {
                DeleteFiber(c->fiber);
                c->fiber = NULL;
        }
}

#        endif /* _BC_WIN */

/* ---- Scheduler ------------------------------------------------- */
CoroScheduler* coro_sched_new(Arena* a, int cap) {
        if (cap <= 0 || cap > CORO_SCHED_MAX) cap = CORO_SCHED_MAX;
        CoroScheduler* s = arena_push_type(a, CoroScheduler);
        memset(s, 0, sizeof *s);
        s->cap = cap;
        return s;
}

void coro_sched_add(CoroScheduler* s, Coro* c) {
        assert(s->count < s->cap && "coro_sched_add: scheduler full");
        s->coros[s->count++] = c;
}

void coro_run_all(CoroScheduler* s) {
        for (;;) {
                int any = 0;
                int i;
                for (i = 0; i < s->count; i++) {
                        Coro* c = s->coros[i];
                        if (!coro_done(c)) {
                                coro_resume(c);
                                any = 1;
                        }
                }
                if (!any) break;
        }
}

int coro_active(CoroScheduler* s) {
        int n = 0, i;
        for (i = 0; i < s->count; i++)
                if (!coro_done(s->coros[i])) n++;
        return n;
}

#endif /* BARECORO_IMPLEMENTATION */
#endif /* BARECORO_H */
