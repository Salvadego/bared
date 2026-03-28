#define BARESTD_IMPLEMENTATION
#define BARESYNC_IMPLEMENTATION
#include <stdio.h>

#include "baresync.h"

static int g_counter = 0;

int main(void) {
        /* --- 1. Mutex: basic lock / unlock --- */
        Mutex mu = {0};
        mutex_lock(&mu);
        g_counter = 42;
        mutex_unlock(&mu);
        printf("mutex guarded write: g_counter=%d\n", g_counter);

        /* --- 2. mutex_trylock --- */
        bool got = mutex_trylock(&mu);
        if (got) {
                g_counter++;
                mutex_unlock(&mu);
        }
        printf("trylock acquired=%d  g_counter=%d\n", (int)got, g_counter);

        /* trylock fails when already held */
        mutex_lock(&mu);
        bool not_got = mutex_trylock(&mu); /* on Linux with default mutex this
                                              may succeed (non-recursive), so
                                              just show the result */
        printf("trylock while held: %d\n", (int)not_got);
        if (not_got) mutex_unlock(&mu);
        mutex_unlock(&mu);

        /* --- 3. RWLock: multiple shared readers --- */
        RWLock rw = {0};
        rwlock_rlock(&rw);
        printf("rlock (shared) acquired\n");
        int snapshot = g_counter; /* read shared data */
        rwlock_runlock(&rw);
        printf("rlock released  snapshot=%d\n", snapshot);

        /* exclusive writer */
        rwlock_wlock(&rw);
        g_counter = 100;
        printf("wlock (exclusive) acquired  g_counter=%d\n", g_counter);
        rwlock_wunlock(&rw);
        printf("wlock released\n");

        /* --- 4. Condvar: signal pattern --- */
        Condvar cv = {0};
        /* signal with no waiter is safe */
        condvar_signal(&cv);
        printf("condvar_signal (no waiter) ok\n");
        condvar_broadcast(&cv);
        printf("condvar_broadcast (no waiter) ok\n");

        /* --- 6. Destroy (safe to call even if never locked) --- */
        mutex_destroy(&mu);
        rwlock_destroy(&rw);
        condvar_destroy(&cv);
        printf("destroy ok\n");

        /* --- 7. Zero-init is always safe --- */
        Mutex   mu2 = {0};
        RWLock  rw2 = {0};
        Condvar cv2 = {0};
        mutex_lock(&mu2);
        mutex_unlock(&mu2);
        rwlock_rlock(&rw2);
        rwlock_runlock(&rw2);
        condvar_signal(&cv2);
        mutex_destroy(&mu2);
        rwlock_destroy(&rw2);
        condvar_destroy(&cv2);
        printf("zero-init structs work correctly\n");

        printf("done.\n");
        return 0;
}
