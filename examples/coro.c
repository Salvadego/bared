#define BARESTD_IMPLEMENTATION
#define BARECORO_IMPLEMENTATION
#include <stdio.h>
#include <string.h>

#include "barecoro.h"

/* ================================================================
 *  Example 1: Counter generator
 * ================================================================ */
static void counter_fn(Coro* self) {
        int* n = (int*)self->arg;
        while (*n < 5) {
                printf("  count: %d\n", *n);
                (*n)++;
                coro_yield(self);
        }
        printf("  counter done\n");
}

/* ================================================================
 *  Example 2: Fibonacci generator
 *  Caller drives it; coroutine stores current value in self->result.
 * ================================================================ */
static void fib_fn(Coro* self) {
        int64_t a = 0, b = 1;
        int     i;
        for (i = 0; i < 10; i++) {
                self->result = (void*)(uintptr_t)(uint64_t)a;
                coro_yield(self);
                int64_t tmp = a + b;
                a           = b;
                b           = tmp;
        }
}

/* ================================================================
 *  Example 3: Pipeline — producer feeds consumer via shared buffer
 * ================================================================ */
typedef struct {
        int   buf[8];
        int   head, tail, len;
        Coro* producer;
        Coro* consumer;
} Pipeline;

static void producer_fn(Coro* self) {
        Pipeline* p = (Pipeline*)self->arg;
        int       i;
        for (i = 0; i < 20; i++) {
                /* Wait until there's space */
                while (p->len >= 8) coro_yield(self);
                p->buf[p->tail % 8] = i * i;
                p->tail++;
                p->len++;
                printf("  produced: %d\n", i * i);
                coro_yield(self);
        }
}

static void consumer_fn(Coro* self) {
        Pipeline* p        = (Pipeline*)self->arg;
        int       consumed = 0;
        while (consumed < 20) {
                while (p->len == 0) coro_yield(self);
                int val = p->buf[p->head % 8];
                p->head++;
                p->len--;
                printf("  consumed: %d\n", val);
                consumed++;
                coro_yield(self);
        }
}

/* ================================================================
 *  Example 4: Cooperative tasks via scheduler
 * ================================================================ */
typedef struct {
        const char* name;
        int         steps;
} TaskArg;

static void task_fn(Coro* self) {
        TaskArg* t = (TaskArg*)self->arg;
        int      i;
        for (i = 0; i < t->steps; i++) {
                printf("  [%s] step %d/%d\n", t->name, i + 1, t->steps);
                coro_yield(self);
        }
}

/* ================================================================
 *  main
 * ================================================================ */
int main(void) {
        Arena* a = arena_new(KB(64));

        /* ---- Counter generator ---- */
        printf("=== Counter generator ===\n");
        int   n       = 0;
        Coro* counter = coro_new(a, counter_fn, &n, KB(64));
        while (!coro_done(counter)) coro_resume(counter);
        coro_free(counter);

        /* ---- Fibonacci generator ---- */
        printf("\n=== Fibonacci ===\n");
        Coro* fib = coro_new(a, fib_fn, NULL, KB(64));
        printf("  fib: ");
        while (!coro_done(fib)) {
                coro_resume(fib);
                if (!coro_done(fib))
                        printf("%lld ", (long long)(uintptr_t)coro_result(fib));
        }
        printf("\n");
        coro_free(fib);

        /* ---- Pipeline ---- */
        printf("\n=== Producer/Consumer pipeline ===\n");
        Pipeline pipe;
        memset(&pipe, 0, sizeof pipe);
        Coro* prod    = coro_new(a, producer_fn, &pipe, KB(64));
        Coro* cons    = coro_new(a, consumer_fn, &pipe, KB(64));
        pipe.producer = prod;
        pipe.consumer = cons;

        CoroScheduler* sched = coro_sched_new(a, 8);
        coro_sched_add(sched, prod);
        coro_sched_add(sched, cons);
        coro_run_all(sched);
        coro_free(prod);
        coro_free(cons);

        /* ---- Multiple tasks via scheduler ---- */
        printf("\n=== Scheduler: 3 tasks ===\n");
        TaskArg        ta = {"A", 3}, tb = {"B", 2}, tc = {"C", 4};
        CoroScheduler* s2 = coro_sched_new(a, 8);
        Coro*          ca = coro_new(a, task_fn, &ta, KB(64));
        Coro*          cb = coro_new(a, task_fn, &tb, KB(64));
        Coro*          cc = coro_new(a, task_fn, &tc, KB(64));
        coro_sched_add(s2, ca);
        coro_sched_add(s2, cb);
        coro_sched_add(s2, cc);
        coro_run_all(s2);
        coro_free(ca);
        coro_free(cb);
        coro_free(cc);

        printf("\n=== All done ===\n");
        arena_free(a);
        return 0;
}
