#define BARESTD_IMPLEMENTATION
#define BARESIG_IMPLEMENTATION
#include "baresig.h"
#include <signal.h>
#include <stdio.h>

int main(void) {
        /* --- 1. sig_catch: install catching handler --- */
        sig_catch(SIGINT);
        sig_catch(SIGTERM);
        sig_catch(SIGUSR1);
        printf("SIGINT, SIGTERM, SIGUSR1 handlers installed\n");

        /* --- 2. sig_received: poll the flag --- */
        printf("sig_received(SIGINT) before raise: %d\n", sig_received(SIGINT));
        raise(SIGINT);
        printf("sig_received(SIGINT) after raise:  %d\n", sig_received(SIGINT));

        /* --- 3. sig_reset: clear one flag --- */
        sig_reset(SIGINT);
        printf("sig_received after sig_reset:      %d\n", sig_received(SIGINT));

        /* --- 4. Multiple signals raised --- */
        raise(SIGTERM);
        raise(SIGUSR1);
        printf("SIGTERM received: %d\n", sig_received(SIGTERM));
        printf("SIGUSR1 received: %d\n", sig_received(SIGUSR1));
        printf("SIGINT  received: %d  (still cleared)\n", sig_received(SIGINT));

        /* --- 5. sig_reset_all: clear every flag --- */
        sig_reset_all();
        printf("after sig_reset_all:\n");
        printf("  SIGTERM=%d  SIGUSR1=%d\n",
               sig_received(SIGTERM),
               sig_received(SIGUSR1));

        /* --- 6. sig_ignore: install SIG_IGN --- */
        sig_ignore(SIGPIPE);
        printf("SIGPIPE ignored (write to closed pipe won't crash)\n");

        /* --- 7. sig_default: restore SIG_DFL --- */
        sig_default(SIGPIPE);
        printf("SIGPIPE restored to default\n");

        /* --- 8. Out-of-range signal is safe --- */
        printf("sig_received(0)   = %d  (no-op)\n", sig_received(0));
        printf("sig_received(-1)  = %d  (no-op)\n", sig_received(-1));
        printf("sig_received(SIG_MAX_TRACK) = %d  (no-op)\n",
               sig_received(SIG_MAX_TRACK));
        sig_reset(0);  /* safe no-op */
        sig_reset(-1); /* safe no-op */

        /* --- 9. Typical "run until signal" pattern --- */
        sig_catch(SIGINT);
        sig_catch(SIGTERM);
        /* In a real program:
         *   while (!sig_received(SIGINT) && !sig_received(SIGTERM)) {
         *       do_work();
         *   }
         * For this example we just verify the loop condition is sane:
         */
        printf("loop condition false (no signal yet): %d\n",
               !sig_received(SIGINT) && !sig_received(SIGTERM));
        raise(SIGINT);
        printf("loop condition true  (SIGINT raised): %d\n",
               sig_received(SIGINT) || sig_received(SIGTERM));

        /* --- 10. Restore to default for clean process exit --- */
        sig_default(SIGINT);
        sig_default(SIGTERM);
        sig_default(SIGUSR1);
        printf("all signals restored to default\n");

        printf("done.\n");
        return 0;
}
