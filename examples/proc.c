#define BARESTD_IMPLEMENTATION
#define STRBUILDER_IMPLEMENTATION
#define BARETIME_IMPLEMENTATION
#define BAREPROC_IMPLEMENTATION
#include "bareproc.h"
#include "baretime.h"

/* Trim trailing newline for cleaner output */
static const char* trimmed(const char* s) {
        static char buf[256];
        size_t      n = strlen(s);
        if (n > sizeof(buf) - 1) n = sizeof(buf) - 1;
        memcpy(buf, s, n);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--;
        buf[n] = '\0';
        return buf;
}

int main(void) {
        Arena* a = arena_new(KB(16));

        /* --- 1. proc_run_stdout: capture stdout, discard stderr --- */
        Str uname = proc_run_stdout(a, str_lit("uname -s"));
        printf("uname -s: \"%s\"\n", trimmed(uname.ptr));

        /* --- 2. proc_run_capture: capture both stdout and stderr --- */
        ProcResult r = proc_run_capture(a, str_lit("echo hello from bare"));
        printf("exit_code = %d\n", r.exit_code);
        printf("stdout    = \"%s\"\n", trimmed(r.out.ptr));

        /* stderr from a command that writes to it */
        ProcResult r2 =
                proc_run_capture(a, str_lit("echo out"));
        printf("stdout = \"%s\"\n", trimmed(r2.out.ptr));
        printf("stderr = \"%s\"\n", trimmed(r2.err.ptr));

        /* --- 3. proc_run_capture: non-zero exit code --- */
        ProcResult r3 = proc_run_capture(a, str_lit("exit 42"));
        printf("sh exit 42: exit_code=%d\n", r3.exit_code);

        /* --- 4. proc_spawn_str + proc_wait --- */
        ProcPipe pipes    = {0};
        pipes.want_stdout = true;
        Proc p = proc_spawn_str(a, str_lit("echo spawned"), &pipes);
        char buf[64];
        memset(buf, 0, sizeof buf);
        proc_read(&pipes, buf, sizeof buf - 1);
        int code = proc_wait(p);
        printf("spawn_str: stdout=\"%s\"  code=%d\n", trimmed(buf), code);

        /* --- 5. proc_spawn_argv: NULL-terminated char* array ---
         *  Pass NULL for env to inherit the current environment.
         *  (Using env=NULL avoids the non-portable execvpe.) */
        const char* argv[] = {"echo", "argv", "spawn", NULL};
        ProcPipe    pp     = {0};
        pp.want_stdout     = true;
        Proc p2            = proc_spawn_argv(a, argv, NULL, &pp);
        char buf2[64];
        memset(buf2, 0, sizeof buf2);
        proc_read(&pp, buf2, sizeof buf2 - 1);
        proc_wait(p2);
        printf("spawn_argv: \"%s\"\n", trimmed(buf2));

        /* --- 6. proc_write to stdin, proc_read from stdout --- */
        const char* cat_argv[] = {"cat", NULL};
        ProcPipe    io_pipe    = {0};
        io_pipe.want_stdin     = true;
        io_pipe.want_stdout    = true;
        Proc cat               = proc_spawn_argv(a, cat_argv, NULL, &io_pipe);
        proc_write(&io_pipe, "pipe round-trip", 15);
        proc_close_stdin(&io_pipe);
        char rbuf[64];
        memset(rbuf, 0, sizeof rbuf);
        proc_read(&io_pipe, rbuf, sizeof rbuf - 1);
        proc_wait(cat);
        printf("cat round-trip: \"%s\"\n", rbuf);

        /* --- 7. proc_read_err: read from stderr pipe --- */
        const char* err_argv[] = {"sh", "-c", "echo stderr-only >&2", NULL};
        ProcPipe    ep         = {0};
        ep.want_stderr         = true;
        Proc ep_proc           = proc_spawn_argv(a, err_argv, NULL, &ep);
        char errbuf[64];
        memset(errbuf, 0, sizeof errbuf);
        proc_read_err(&ep, errbuf, sizeof errbuf - 1);
        proc_wait(ep_proc);
        printf("stderr pipe: \"%s\"\n", trimmed(errbuf));

        /* --- 8. proc_running: non-blocking check --- */
        const char* sleep_argv[] = {"sleep", "1", NULL};
        Proc        lp           = proc_spawn_argv(a, sleep_argv, NULL, NULL);
        int         running      = proc_running(lp);
        printf("sleep 1 running immediately: %d\n", running);

        /* --- 9. proc_kill --- */
        proc_kill(lp);
        int kcode = proc_wait(lp);
        printf("after kill: exit_code=%d  (non-zero=%d)\n", kcode, kcode != 0);

        /* --- 10. No-pipe spawn (just fire and wait) --- */
        const char* ls_argv[] = {"true", NULL};
        Proc        nopipe    = proc_spawn_argv(a, ls_argv, NULL, NULL);
        printf("no-pipe spawn, wait code=%d\n", proc_wait(nopipe));

        /* --- 11. Multi-word command via proc_spawn_str --- */
        Str out2 = proc_run_stdout(a, str_lit("printf '%s' hello"));
        printf("printf output: \"" StrFmt "\"\n", StrArgs(out2));

        arena_free(a);
        printf("done.\n");
        return 0;
}
