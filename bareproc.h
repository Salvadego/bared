/*
 * bareproc.h -- Process spawn + pipes
 * =====================================
 *
 *  USAGE
 *    #define BAREPROC_IMPLEMENTATION
 *    #include "bareproc.h"
 *
 *  DEPENDS ON
 *    barestd.h, baresig.h (optional -- for SIGCHLD handling)
 *
 *  PLATFORM
 *    POSIX   -- fork/execvp/pipe/waitpid
 *    Windows -- CreateProcess/HANDLE pipes
 *
 *  DESIGN
 *    Proc     -- a spawned child process handle.
 *    ProcPipe -- connected stdin/stdout/stderr pipes, optional per-field.
 *
 *    argv is passed as Slice(Str); the implementation converts to a
 *    NULL-terminated char* array on the stack (up to PROC_ARGV_MAX args)
 *    with no heap allocation.  For longer argv, increase PROC_ARGV_MAX.
 *
 *    Scratch arenas are used for any temporary string work inside
 *    proc_run_capture so the caller's arena receives only the final
 *    output and the scratch is freed immediately after.
 *
 *    proc_run_capture runs a command, collects stdout into arena,
 *    and waits for exit.  It uses a fixed 4 KB stack read buffer --
 *    no VLA, no malloc.
 *
 *  EXAMPLE -- spawn and wait
 *
 *    Arena *a = arena_new(KB(64));
 *    const char *argv[] = {"ls", "-la", NULL};
 *    Proc p = proc_spawn_argv(a, argv, NULL);
 *    int code = proc_wait(p);
 *
 *  EXAMPLE -- piped I/O
 *
 *    ProcPipe pipes = {0};
 *    pipes.want_stdout = true;
 *    Proc p = proc_spawn_str(a, str_lit("cat /etc/hostname"), &pipes);
 *    char buf[256];
 *    int n = proc_read(pipes.stdout_r, buf, sizeof buf);
 *    int code = proc_wait(p);
 *
 *  EXAMPLE -- capture output
 *
 *    ProcResult r = proc_run_capture(a, str_lit("uname -a"));
 *    printf("exit=%d out=" StrFmt "\n", r.exit_code, StrArgs(r.out));
 */

/* _GNU_SOURCE must be defined before any system header. */
#if !defined(_GNU_SOURCE) && (defined(__linux__) || defined(__GLIBC__))
#  define _GNU_SOURCE
#endif

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
#ifndef BAREPROC_H
#        define BAREPROC_H

#        include <stdint.h>

#        include "barestd.h"

/* Maximum argv slots (on the stack -- adjust if needed). */
#        define PROC_ARGV_MAX 64

/* ================================================================
 *  Platform setup
 * ================================================================ */

#        if defined(_WIN32) || defined(_WIN64)
#                define _BP_WIN
#                ifndef WIN32_LEAN_AND_MEAN
#                        define WIN32_LEAN_AND_MEAN
#                endif
#                include <windows.h>
typedef HANDLE _BpPipe;
typedef HANDLE _BpPid;
#                define BP_PIPE_INVALID INVALID_HANDLE_VALUE
#        else
#                define _BP_POSIX
#                include <errno.h>
#                include <sys/wait.h>
#                include <unistd.h>
typedef int _BpPipe;
typedef int _BpPid;
#                define BP_PIPE_INVALID (-1)
#        endif

/* ================================================================
 *  Types
 * ================================================================ */

typedef struct {
        _BpPid _pid;
#        ifdef _BP_WIN
        HANDLE _hproc;
#        endif
} Proc;

typedef struct {
        /* Set to true before spawn to request that pipe. */
        bool want_stdin;
        bool want_stdout;
        bool want_stderr;

        /* Filled by proc_spawn_* after success. */
        _BpPipe stdin_w;  /* write end of child's stdin  */
        _BpPipe stdout_r; /* read  end of child's stdout */
        _BpPipe stderr_r; /* read  end of child's stderr */
} ProcPipe;

typedef struct {
        Str out; /* captured stdout (arena-owned) */
        Str err; /* captured stderr (arena-owned) */
        int exit_code;
} ProcResult;

/* ================================================================
 *  API
 * ================================================================ */

/*
 * Spawn from a NULL-terminated char* array.
 * argv[0] is the program; searched in PATH.
 * env is a NULL-terminated char* array of "KEY=VAL" strings,
 * or NULL to inherit the current environment.
 * pipes may be NULL if you don't need piped I/O.
 */
Proc proc_spawn_argv(Arena*      a,
                     const char* argv[],
                     const char* env[],
                     ProcPipe*   pipes);

/*
 * Spawn from a Str command line, split on whitespace.
 * Uses a scratch arena to build the argv -- freed before return.
 * env may be NULL (inherit).
 */
Proc proc_spawn_str(Arena* a, Str cmd, ProcPipe* pipes);

Proc proc_spawn_shell(Arena *a, Str cmd, ProcPipe *pipes);

/*
 * Wait for proc to exit.  Returns the exit code (0 = success).
 * On signal termination returns 128 + signal number (POSIX convention).
 */
int proc_wait(Proc p);

/*
 * Kill the process.
 * POSIX: sends SIGKILL.  Windows: TerminateProcess(exit_code=1).
 */
void proc_kill(Proc p);

/* Check if process is still running (non-blocking). */
bool proc_running(Proc p);

/* Write to child's stdin pipe.  Returns false on error. */
bool proc_write(ProcPipe* pipes, const void* buf, size_t n);

/* Read from child's stdout pipe.  Returns bytes read, 0=EOF, -1=error. */
int proc_read(ProcPipe* pipes, void* buf, size_t cap);

/* Read from child's stderr pipe. */
int proc_read_err(ProcPipe* pipes, void* buf, size_t cap);

/* Close a write-end pipe (signal EOF to child). */
void proc_close_stdin(ProcPipe* pipes);

/*
 * Run cmd, collect stdout+stderr, wait for exit.
 * Uses a 4 KB static read buffer internally -- no VLA, no heap.
 * Scratch used to collect chunks; final output copied to a once.
 */
ProcResult proc_run_capture(Arena* a, Str cmd);

/*
 * Same but only captures stdout.
 * Slightly cheaper -- stderr goes to /dev/null or is discarded.
 */
Str proc_run_stdout(Arena* a, Str cmd);

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#        ifdef BAREPROC_IMPLEMENTATION

#                include <stdio.h>
#                include <string.h>

/* Static 4 KB read buffer shared across all proc_run_capture calls.
   Not thread-safe if called concurrently -- wrap with a mutex if needed. */
static char _bp_readbuf[4096];

/* ---- Arg splitting ------------------------------------------- */

/*
 * Split s on whitespace into at most PROC_ARGV_MAX-1 tokens.
 * Writes char* pointers into out_argv (must have PROC_ARGV_MAX slots).
 * Uses scratch_arena for the NUL-terminated copies.
 * Returns the count (excluding the terminating NULL).
 */
static int _bp_split_args(Arena*      scratch,
                          Str         s,
                          const char* out_argv[PROC_ARGV_MAX]) {
        int n = 0;
        while (s.len && n < PROC_ARGV_MAX - 1) {
                /* skip whitespace */
                while (s.len && (unsigned char)s.ptr[0] <= ' ') {
                        s.ptr++;
                        s.len--;
                }
                if (!s.len) break;
                /* find end of token */
                size_t i = 0;
                while (i < s.len && (unsigned char)s.ptr[i] > ' ') i++;
                out_argv[n++] = str_to_cstr(scratch, str_buf(s.ptr, i));
                s.ptr += i;
                s.len -= i;
        }
        out_argv[n] = NULL;
        return n;
}

/* ---- POSIX ---------------------------------------------------- */
#                ifdef _BP_POSIX

#                        include <fcntl.h>

static void _bp_pipe_pair(_BpPipe fds[2]) {
        int r = pipe(fds);
        assert(r == 0 && "pipe() failed");
        Unused(r);
}

static void _bp_close_pipe(_BpPipe fd) {
        if (fd != BP_PIPE_INVALID) close(fd);
}

Proc proc_spawn_argv(Arena*      a,
                     const char* argv[],
                     const char* env[],
                     ProcPipe*   pipes) {
        Unused(a);
        _BpPipe in_fds[2]  = {BP_PIPE_INVALID, BP_PIPE_INVALID};
        _BpPipe out_fds[2] = {BP_PIPE_INVALID, BP_PIPE_INVALID};
        _BpPipe err_fds[2] = {BP_PIPE_INVALID, BP_PIPE_INVALID};

        if (pipes && pipes->want_stdin) _bp_pipe_pair(in_fds);
        if (pipes && pipes->want_stdout) _bp_pipe_pair(out_fds);
        if (pipes && pipes->want_stderr) _bp_pipe_pair(err_fds);

        pid_t pid = fork();
        assert(pid >= 0 && "fork() failed");

        if (pid == 0) {
                /* ---- child ---- */
                if (in_fds[0] != BP_PIPE_INVALID) {
                        dup2(in_fds[0], 0);
                        _bp_close_pipe(in_fds[0]);
                        _bp_close_pipe(in_fds[1]);
                }
                if (out_fds[1] != BP_PIPE_INVALID) {
                        dup2(out_fds[1], 1);
                        _bp_close_pipe(out_fds[0]);
                        _bp_close_pipe(out_fds[1]);
                }
                if (err_fds[1] != BP_PIPE_INVALID) {
                        dup2(err_fds[1], 2);
                        _bp_close_pipe(err_fds[0]);
                        _bp_close_pipe(err_fds[1]);
                }

                if (env)
                        execvpe(argv[0], (char* const*)argv, (char* const*)env);
                else
                        execvp(argv[0], (char* const*)argv);
                _exit(127); /* exec failed */
        }

        /* ---- parent ---- */
        _bp_close_pipe(in_fds[0]);
        _bp_close_pipe(out_fds[1]);
        _bp_close_pipe(err_fds[1]);

        if (pipes) {
                pipes->stdin_w  = in_fds[1];
                pipes->stdout_r = out_fds[0];
                pipes->stderr_r = err_fds[0];
        }

        Proc p;
        p._pid = pid;
        return p;
}

Proc proc_spawn_str(Arena* a, Str cmd, ProcPipe* pipes) {
        Scratch     sc = scratch_begin(a);
        const char* argv[PROC_ARGV_MAX];
        _bp_split_args(sc.a, cmd, argv);
        Proc p = proc_spawn_argv(a, argv, NULL, pipes);
        scratch_end(sc);
        return p;
}

Proc proc_spawn_shell(Arena *a, Str cmd, ProcPipe *pipes) {
    const char *argv[] = {"sh", "-c", str_to_cstr(a, cmd), NULL};
    return proc_spawn_argv(a, argv, NULL, pipes);
}

int proc_wait(Proc p) {
        int status = 0;
        while (waitpid(p._pid, &status, 0) < 0 && errno == EINTR) {
        }
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
        return -1;
}

void proc_kill(Proc p) {
        kill(p._pid, 9);
}

bool proc_running(Proc p) {
        int status;
        return waitpid(p._pid, &status, WNOHANG) == 0;
}

bool proc_write(ProcPipe* pipes, const void* buf, size_t n) {
        const char* p = (const char*)buf;
        while (n > 0) {
                ssize_t w;
                do {
                        w = write(pipes->stdin_w, p, n);
                } while (w < 0 && errno == EINTR);
                if (w <= 0) return false;
                p += w;
                n -= (size_t)w;
        }
        return true;
}

int proc_read(ProcPipe* pipes, void* buf, size_t cap) {
        ssize_t r;
        do {
                r = read(pipes->stdout_r, buf, cap);
        } while (r < 0 && errno == EINTR);
        return (int)r;
}

int proc_read_err(ProcPipe* pipes, void* buf, size_t cap) {
        ssize_t r;
        do {
                r = read(pipes->stderr_r, buf, cap);
        } while (r < 0 && errno == EINTR);
        return (int)r;
}

void proc_close_stdin(ProcPipe* pipes) {
        _bp_close_pipe(pipes->stdin_w);
        pipes->stdin_w = BP_PIPE_INVALID;
}

ProcResult proc_run_capture(Arena* a, Str cmd) {
        ProcPipe pipes    = {0};
        pipes.want_stdout = true;
        pipes.want_stderr = true;

        Proc p = proc_spawn_str(a, cmd, &pipes);

        /* Use scratch to accumulate chunks, then copy final result to a.
           This means the scratch region is freed after we copy, saving
           the working-set memory during collection. */
        Scratch sc_out  = scratch_begin(a);
        Slice(char) out = slice_make(sc_out.a, char, 1024);
        Slice(char) err = slice_make(sc_out.a, char, 256);

        /* Collect until both pipes close. */
        bool out_open = true, err_open = true;
        while (out_open || err_open) {
                if (out_open) {
                        ssize_t n;
                        do {
                                n = read(pipes.stdout_r,
                                         _bp_readbuf,
                                         sizeof _bp_readbuf);
                        } while (n < 0 && errno == EINTR);
                        if (n <= 0) {
                                close(pipes.stdout_r);
                                out_open = false;
                        } else {
                                size_t i;
                                for (i = 0; i < (size_t)n; i++)
                                        slice_push(out, char, _bp_readbuf[i]);
                        }
                }
                if (err_open) {
                        ssize_t n;
                        do {
                                n = read(pipes.stderr_r,
                                         _bp_readbuf,
                                         sizeof _bp_readbuf);
                        } while (n < 0 && errno == EINTR);
                        if (n <= 0) {
                                close(pipes.stderr_r);
                                err_open = false;
                        } else {
                                size_t i;
                                for (i = 0; i < (size_t)n; i++)
                                        slice_push(err, char, _bp_readbuf[i]);
                        }
                }
        }

        /* Copy results into permanent arena allocation. */
        size_t olen = slice_len(out), elen = slice_len(err);
        char*  obuf = arena_push_array(a, char, olen + 1);
        char*  ebuf = arena_push_array(a, char, elen + 1);
        memcpy(obuf, out, olen);
        obuf[olen] = '\0';
        memcpy(ebuf, err, elen);
        ebuf[elen] = '\0';
        scratch_end(sc_out); /* free working slices */

        ProcResult r;
        r.out       = str_buf(obuf, olen);
        r.err       = str_buf(ebuf, elen);
        r.exit_code = proc_wait(p);
        return r;
}

Str proc_run_stdout(Arena* a, Str cmd) {
        ProcPipe pipes    = {0};
        pipes.want_stdout = true;

        Proc p = proc_spawn_str(a, cmd, &pipes);
        close(pipes.stderr_r); /* discard stderr */

        Scratch sc      = scratch_begin(a);
        Slice(char) out = slice_make(sc.a, char, 1024);
        ssize_t n;
        while ((n = read(pipes.stdout_r, _bp_readbuf, sizeof _bp_readbuf)) >
               0) {
                size_t i;
                for (i = 0; i < (size_t)n; i++)
                        slice_push(out, char, _bp_readbuf[i]);
        }
        close(pipes.stdout_r);

        size_t olen = slice_len(out);
        char*  buf  = arena_push_array(a, char, olen + 1);
        memcpy(buf, out, olen);
        buf[olen] = '\0';
        scratch_end(sc);

        proc_wait(p);
        return str_buf(buf, olen);
}

#                endif /* _BP_POSIX */

/* ---- Windows ------------------------------------------------- */
#                ifdef _BP_WIN

static void _bp_close_pipe(_BpPipe h) {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

Proc proc_spawn_argv(Arena*      a,
                     const char* argv[],
                     const char* env[],
                     ProcPipe*   pipes) {
        /* Build command line from argv into a scratch buffer. */
        Scratch    sc = scratch_begin(a);
        StrBuilder sb = sb_make(sc.a, 256);
        int        i;
        for (i = 0; argv[i]; i++) {
                if (i) sb_write_char(&sb, ' ');
                /* Naive quoting: wrap in quotes if contains spaces. */
                bool has_space = (strchr(argv[i], ' ') != NULL);
                if (has_space) sb_write_char(&sb, '"');
                sb_write_cstr(&sb, argv[i]);
                if (has_space) sb_write_char(&sb, '"');
        }
        char* cmdline = sb_to_cstr(sc.a, &sb);

        /* Build env block */
        char* envblock = NULL;
        if (env) {
                /* env is NULL-terminated char*[]; build a flat double-null
                 * block */
                StrBuilder eb = sb_make(sc.a, 512);
                for (i = 0; env[i]; i++) {
                        sb_write_cstr(&eb, env[i]);
                        sb_write_char(&eb, '\0');
                }
                sb_write_char(&eb, '\0');
                /* to_cstr adds a NUL but we built our own -- use raw buf */
                envblock = (char*)eb.buf;
        }

        HANDLE in_r = NULL, in_w = NULL, out_r = NULL, out_w = NULL,
               err_r = NULL, err_w = NULL;
        SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};

        if (pipes && pipes->want_stdin) {
                CreatePipe(&in_r, &in_w, &sa, 0);
                SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
        }
        if (pipes && pipes->want_stdout) {
                CreatePipe(&out_r, &out_w, &sa, 0);
                SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
        }
        if (pipes && pipes->want_stderr) {
                CreatePipe(&err_r, &err_w, &sa, 0);
                SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);
        }

        STARTUPINFOA si = {0};
        si.cb           = sizeof si;
        si.dwFlags      = STARTF_USESTDHANDLES;
        si.hStdInput    = in_r ? in_r : GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput   = out_w ? out_w : GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError    = err_w ? err_w : GetStdHandle(STD_ERROR_HANDLE);

        PROCESS_INFORMATION pi = {0};
        BOOL ok = CreateProcessA(NULL,
                                 cmdline,
                                 NULL,
                                 NULL,
                                 TRUE,
                                 envblock ? CREATE_UNICODE_ENVIRONMENT : 0,
                                 envblock,
                                 NULL,
                                 &si,
                                 &pi);
        scratch_end(sc);

        _bp_close_pipe(in_r);
        _bp_close_pipe(out_w);
        _bp_close_pipe(err_w);

        assert(ok && "CreateProcess failed");

        if (pipes) {
                pipes->stdin_w  = in_w;
                pipes->stdout_r = out_r;
                pipes->stderr_r = err_r;
        }

        Proc p;
        p._pid   = pi.hProcess;
        p._hproc = pi.hProcess;
        CloseHandle(pi.hThread);
        return p;
}

Proc proc_spawn_str(Arena* a, Str cmd, ProcPipe* pipes) {
        Scratch     sc = scratch_begin(a);
        const char* argv[PROC_ARGV_MAX];
        _bp_split_args(sc.a, cmd, argv);
        Proc p = proc_spawn_argv(a, argv, NULL, pipes);
        scratch_end(sc);
        return p;
}

int proc_wait(Proc p) {
        WaitForSingleObject(p._hproc, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(p._hproc, &code);
        CloseHandle(p._hproc);
        return (int)code;
}

void proc_kill(Proc p) {
        TerminateProcess(p._hproc, 1);
}

bool proc_running(Proc p) {
        DWORD code = STILL_ACTIVE;
        GetExitCodeProcess(p._hproc, &code);
        return code == STILL_ACTIVE;
}

static bool _bp_win_write(HANDLE h, const void* buf, size_t n) {
        const char* p = (const char*)buf;
        while (n > 0) {
                DWORD w = 0;
                if (!WriteFile(h, p, (DWORD)n, &w, NULL) || w == 0)
                        return false;
                p += w;
                n -= w;
        }
        return true;
}
static int _bp_win_read(HANDLE h, void* buf, size_t cap) {
        DWORD r = 0;
        if (!ReadFile(h, buf, (DWORD)cap, &r, NULL)) return (r == 0) ? 0 : -1;
        return (int)r;
}

bool proc_write(ProcPipe* pipes, const void* buf, size_t n) {
        return _bp_win_write(pipes->stdin_w, buf, n);
}
int proc_read(ProcPipe* pipes, void* buf, size_t cap) {
        return _bp_win_read(pipes->stdout_r, buf, cap);
}
int proc_read_err(ProcPipe* pipes, void* buf, size_t cap) {
        return _bp_win_read(pipes->stderr_r, buf, cap);
}

void proc_close_stdin(ProcPipe* pipes) {
        _bp_close_pipe(pipes->stdin_w);
        pipes->stdin_w = INVALID_HANDLE_VALUE;
}

ProcResult proc_run_capture(Arena* a, Str cmd) {
        ProcPipe pipes    = {0};
        pipes.want_stdout = pipes.want_stderr = true;
        Proc p                                = proc_spawn_str(a, cmd, &pipes);

        Scratch sc      = scratch_begin(a);
        Slice(char) out = slice_make(sc.a, char, 1024);
        Slice(char) err = slice_make(sc.a, char, 256);

        bool out_open = true, err_open = true;
        while (out_open || err_open) {
                if (out_open) {
                        int n = _bp_win_read(
                            pipes.stdout_r, _bp_readbuf, sizeof _bp_readbuf);
                        if (n <= 0) {
                                CloseHandle(pipes.stdout_r);
                                out_open = false;
                        } else {
                                int i;
                                for (i = 0; i < n; i++)
                                        slice_push(out, char, _bp_readbuf[i]);
                        }
                }
                if (err_open) {
                        int n = _bp_win_read(
                            pipes.stderr_r, _bp_readbuf, sizeof _bp_readbuf);
                        if (n <= 0) {
                                CloseHandle(pipes.stderr_r);
                                err_open = false;
                        } else {
                                int i;
                                for (i = 0; i < n; i++)
                                        slice_push(err, char, _bp_readbuf[i]);
                        }
                }
        }

        size_t olen = slice_len(out), elen = slice_len(err);
        char*  obuf = arena_push_array(a, char, olen + 1);
        char*  ebuf = arena_push_array(a, char, elen + 1);
        memcpy(obuf, out, olen);
        obuf[olen] = '\0';
        memcpy(ebuf, err, elen);
        ebuf[elen] = '\0';
        scratch_end(sc);

        ProcResult r;
        r.out       = str_buf(obuf, olen);
        r.err       = str_buf(ebuf, elen);
        r.exit_code = proc_wait(p);
        return r;
}

Str proc_run_stdout(Arena* a, Str cmd) {
        ProcPipe pipes    = {0};
        pipes.want_stdout = true;
        Proc p            = proc_spawn_str(a, cmd, &pipes);
        CloseHandle(pipes.stderr_r);

        Scratch sc      = scratch_begin(a);
        Slice(char) out = slice_make(sc.a, char, 1024);
        int n;
        while ((n = _bp_win_read(
                    pipes.stdout_r, _bp_readbuf, sizeof _bp_readbuf)) > 0) {
                int i;
                for (i = 0; i < n; i++) slice_push(out, char, _bp_readbuf[i]);
        }
        CloseHandle(pipes.stdout_r);

        size_t olen = slice_len(out);
        char*  buf  = arena_push_array(a, char, olen + 1);
        memcpy(buf, out, olen);
        buf[olen] = '\0';
        scratch_end(sc);

        proc_wait(p);
        return str_buf(buf, olen);
}

#                endif /* _BP_WIN */
#        endif         /* BAREPROC_IMPLEMENTATION */
#endif                 /* BAREPROC_H */
