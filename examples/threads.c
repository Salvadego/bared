#define BARESTD_IMPLEMENTATION
#define BARESYNC_IMPLEMENTATION
#define BARETHREADS_IMPLEMENTATION
#define BARETIME_IMPLEMENTATION
#include "baretime.h"
#include "barethreads.h"
#include <stdio.h>


/* --- Shared state for worker threads --- */
static Mutex  g_mu = {0};
static int    g_results[8];
static TLSKey g_tls;

/* Worker: stores id^2 in results array and returns id via void* */
static void* worker(void* arg) {
        int id = (int)(intptr_t)arg;
        time_sleep(1 * MS);
        mutex_lock(&g_mu);
        g_results[id] = id * id;
        mutex_unlock(&g_mu);
        return (void*)(intptr_t)id;
}

/* TLS worker: stores its own thread_id in a TLS slot and returns it */
static void* tls_worker(void* arg) {
        (void)arg;
        uint64_t my_id = thread_id();
        tls_set(g_tls, (void*)(uintptr_t)my_id);
        time_sleep(2 * MS);
        return tls_get(g_tls);
}

/* Detached worker: runs independently, no return value used */
static void* detach_worker(void* arg) {
        (void)arg;
        time_sleep(5 * MS);
        return NULL;
}

int main(void) {
        Arena* a = arena_new(KB(4));

        /* --- 1. thread_id: unique main thread ID --- */
        printf("main thread_id = %llu\n", (unsigned long long)thread_id());

        /* --- 2. Spawn + join with return value --- */
        printf("\nspawn + join:\n");
        Thread threads[4];
        for (int i = 0; i < 4; i++) {
                threads[i] = thread_spawn(a, worker, (void*)(intptr_t)i);
        }
        for (int i = 0; i < 4; i++) {
                void* ret = NULL;
                thread_join(threads[i], &ret);
                printf("  thread %d: result=%d  retval=%d\n",
                       i,
                       g_results[i],
                       (int)(intptr_t)ret);
        }

        /* --- 3. thread_join with NULL ret_out --- */
        Thread t_noret = thread_spawn(a, worker, (void*)(intptr_t)0);
        thread_join(t_noret, NULL);
        printf("join with NULL ret_out: ok\n");

        /* --- 4. TLS: each thread sees its own value --- */
        printf("\nTLS:\n");
        g_tls = tls_alloc();
        tls_set(g_tls, (void*)(uintptr_t)0xDEAD); /* main sets its own value */

        Thread t1 = thread_spawn(a, tls_worker, NULL);
        Thread t2 = thread_spawn(a, tls_worker, NULL);
        void * r1 = NULL, *r2 = NULL;
        thread_join(t1, &r1);
        thread_join(t2, &r2);

        void* main_val = tls_get(g_tls);
        printf("  main  tls = 0x%llX\n",
               (unsigned long long)(uintptr_t)main_val);
        printf("  t1    tls = %llu\n", (unsigned long long)(uintptr_t)r1);
        printf("  t2    tls = %llu\n", (unsigned long long)(uintptr_t)r2);
        printf("  t1 != t2: %d\n", r1 != r2);
        printf("  main != t1: %d\n", main_val != r1);

        tls_free(g_tls);

        /* --- 5. tls_get / tls_set round-trip --- */
        TLSKey k     = tls_alloc();
        int    dummy = 42;
        tls_set(k, &dummy);
        int* got = (int*)tls_get(k);
        printf("\ntls round-trip: %d\n", got ? *got : -1);
        tls_free(k);

        /* --- 6. thread_detach (fire and forget) --- */
        printf("\ndetach:\n");
        Thread det = thread_spawn(a, detach_worker, NULL);
        thread_detach(det);
        printf("detached thread running\n");
        time_sleep(10 * MS); /* give it time to finish */
        printf("detached thread finished (no join needed)\n");

        /* --- 7. thread_yield --- */
        thread_yield();
        printf("thread_yield returned\n");

        mutex_destroy(&g_mu);
        arena_free(a);
        printf("done.\n");
        return 0;
}
