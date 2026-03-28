# bared - small C99 utility headers

single-header libs for low-level C.

no build system. no deps (except libc). no bullshit.

---

## usage

in **one** .c file:

```c
#define BAREXXX_IMPLEMENTATION
#include "barexxx.h"
```

everywhere else:

```c
#include "barexxx.h"
```

---

## include order

**always put `baretime.h` first** if you use it - it sets `_POSIX_C_SOURCE`
before glibc locks features. the safest pattern for any file:

```c
#define BARESTD_IMPLEMENTATION
#define BARETIME_IMPLEMENTATION
#define BAREOS_IMPLEMENTATION
// ... rest of _IMPLEMENTATION defines ...
#include "baretime.h"   // first
#include "barestd.h"
#include "bareos.h"
// ...
```

---

## linker flags

| module                              | flag                      |
| ---                                 | ---                       |
| baresync, barethreads, barecoro     | `-lpthread` (Linux/macOS) |
| baremath                            | `-lm`                     |
| barenet                             | `-lws2_32` (Windows only) |

---

## example

```c
#define BARESTD_IMPLEMENTATION
#define BAREBUILDER_IMPLEMENTATION
#include "barestd.h"
#include "barebuilder.h"

int main(void) {
    Arena *a = arena_new(KB(64));

    StrBuilder sb = sb_make(a, 0);
    sb_write_cstr(&sb, "hello ");
    sb_write_fmt(&sb, "%d", 42);

    char *s = sb_to_cstr(a, &sb);   /* "hello 42" in arena */
    arena_free(a);
}
```

---

## modules

### core


**barestd.h** - everything else depends on this

- arena bump allocator with overflow chaining
- scratch scopes (`scratch_begin` / `scratch_end`)
- `Slice(T)` - fat pointer growable array with shadow header
- `Str` - non-owning UTF-8 string view
- `Map` - Robin Hood open-addressing hash map
- number parsing (`str_to_i64`, `str_consume_f64`, ...)
- `str_fmt` - arena-backed printf

---

**barestrs.h** - higher-level string operations

- search: `str_last_find`, `str_index_any`, `str_index_func`, ...
- trim: `str_trim_space`, `str_trim_func`, prefix/suffix variants
- cut: `str_cut`, `str_cut_prefix`, `str_cut_suffix`
- split: `str_split`, `str_fields`, `str_fields_func`
- build: `str_join`, `str_repeat`, `str_replace_all`, `str_to_upper/lower`, `str_title`

---

**bareutf8.h** - UTF-8

- `Rune` type with codepoint + encoded bytes
- decode / encode / width / length
- `Utf8Iter` zero-allocation cursor
- rune predicates and case conversion

---

**baretime.h** - time (include this first)

- `Duration` in nanoseconds with `NS/US/MS/SEC/MIN/HOUR` constants
- `Instant` - monotonic clock, `instant_now()`, `instant_since()`
- `DateTime` - calendar with UTC offset
- `duration_fmt`, `datetime_fmt`, `time_sleep`

---

### data structures

**barebuf.h** - ring buffer

- `RingBuf` - arena-backed byte ring, power-of-two capacity
- `TypedRingBuf(T, N)` - stack/static typed ring, zero allocation, macro API
- read / write / peek / discard

---

**barepool.h** - fixed-size object pool

- pre-allocated slab from arena
- O(1) alloc and O(1) free via intrusive free list
- `pool_reset` returns all objects at once

---

### building / formatting

**barebuilder.h** - string builder

- scratch-backed growth, zero permanent cost until finalize
- `sb_write_char`, `sb_write_str`, `sb_write_fmt`, `sb_write_repeat`
- `sb_to_str` - non-owning view (scratch still live)
- `sb_to_cstr` - copy to arena, release scratch

---

### IO / system

**bareio.h** - reader / writer vtable

- `Reader` / `Writer` thin vtable structs
- `BufReader` / `BufWriter` with configurable buffer
- `MemWriter` growable arena buffer
- `writer_tee` / `writer_null` - broadcast or discard
- `buf_read_line` for line-by-line reading
- typed binary I/O: `io_write_u32le`, `io_read_u16be`, `io_write_pstr16`, ...
- `io_copy`, `io_printf`, `io_read_exactly`
- optional OS backend (include `bareos.h` first to unlock)

---

**bareos.h** - OS layer

- open / create / close / stat / lstat
- read / write entire files
- readdir, glob, mkdir, mkdir\_all, rename, remove
- symlink reading, getenv, getcwd
- `FileInfo` with kind, size, mtime, permissions, uid/gid

---

**barepath.h** - path utilities

- decomposition: `path_dir`, `path_base`, `path_ext`, `path_stem`
- `path_join`, `path_join_slice`, `path_clean`
- `path_rel`, `path_with_ext`
- accepts backslashes, normalises to `/`

---

### concurrency

**baresync.h** - synchronisation primitives

- `Mutex` - non-recursive exclusive lock
- `RWLock` - many readers xor one writer
- `Condvar` - condition variable
- `Once` - run exactly once across all threads
- all zero-init safe (`{0}`), zero allocation

---

**barethreads.h** - threading

- `Thread` - spawn / join / detach
- `TLSKey` - thread-local storage slots
- `thread_id`, `thread_yield`
- spawn uses one 16-byte malloc for the ctx, freed inside the new thread before `fn` runs

---

**barecoro.h** - stackful coroutines

- `Coro` - full C stack, yield at any call depth
- `coro_new`, `coro_resume`, `coro_yield`, `coro_done`, `coro_free`
- `coro_result` - return value after completion
- `CoroScheduler` - cooperative round-robin over N coroutines
- POSIX: `ucontext_t`. Windows: Fibers
- link with `-lpthread`

---

### processes / signals

**bareproc.h** - process spawning

- `proc_spawn_str`, `proc_spawn_argv`
- `proc_spawn_shell` - run via `sh -c`
- piped stdin / stdout / stderr
- `proc_run_capture` - collect stdout+stderr, wait
- `proc_run_stdout` - collect stdout only
- `proc_wait`, `proc_kill`, `proc_running`
- static 4 KB read buffer, no VLA, scratch used for collection

---

**baresig.h** - signal handling

- `sig_catch`, `sig_ignore`, `sig_default`
- `sig_received` / `sig_reset` - poll volatile flag array
- `sig_wait_any` - block until any caught signal fires
- handlers are async-signal-safe (only write a flag)

---

### networking

**barenet.h** - sockets

- `Sock` - thin handle, `SOCK_INVALID`, `sock_valid`
- `NetAddr` - packed address+port, no OS types exposed
- TCP: `net_tcp_listen`, `net_tcp_connect`, `net_accept`
- `net_send` / `net_recv` - retry on EINTR
- `net_send_msg` / `net_recv_msg` - 4-byte length-prefixed framing
- UDP: `net_udp_bind`, `net_udp_socket`, `net_sendto`, `net_recvfrom`
- socket options: nodelay, keepalive, reuseaddr, reuseport, timeouts, nonblocking
- `net_poll` - poll N sockets for READ/WRITE/ERR events with timeout
- `net_wait_readable` / `net_wait_writable` - single-socket readiness
- `ConnPool` - lazy-connecting reusable socket pool over barepool
- static 128-byte error buffer, hot paths allocate nothing

---

**barehttp.h** - HTTP/1.1 server + client

- server: `HttpMux`, prefix router, `http_handle_ctx`, `http_handle_ctx_method`
- handlers get a per-request `Arena`, shared `ctx->user` for app state
- `http_json`, `http_text`, `http_html`, `http_redirect`, `http_not_found`
- `http_path_seg`, `http_query`, `http_header` request accessors
- `http_serve` (thread-per-conn) and `http_serve_pool` (fixed thread pool)
- client: `http_get`, `http_post`, `http_request` with redirect following
- `HttpClientOpts` - timeout, content-type, extra headers
- requires baretime, barenet, baresync, barethreads, barebuilder

---

### math

**baremath.h** - comprehensive math

- constants, type punning (`F32`/`F64` unions)
- special values, basic float ops, trig, hyperbolic, exp/log, power/root
- FP utilities: frexp, ldexp, fma, nextafter
- special functions: erf, erfinv, gamma, lgamma, Bessel
- integer math: gcd, lcm, isqrt, icbrt, next\_pow2, log2\_floor
- bit ops: popcount, clz, ctz, bswap, rol, ror, bit\_extract, bit\_insert, reverse, next\_perm
- overflow-checked arithmetic for u32/u64/i32/i64
- fixed-point: `Q16_16` and `Q8_24` with full arithmetic
- vectors: `Vec2/3/4`, `Vec2i/3i`, `Vec2d/3d/4d` - plain structs, named fields
- quaternion: mul, conj, slerp, rotate\_vec3
- `Mat4` column-major: perspective, look\_at, translate, scale, rotate, inverse
- hashing: SipHash-1-3, xxHash32, xxHash64, Murmur3-32

---

### serialisation

**barejson.h** - JSON parse tree + emitter

- `json_parse(a, src)` - one call, arena-backed value tree, zero-copy strings
- `json_get`, `json_at`, `json_str`, `json_int`, `json_bool`, `json_num` - safe accessors, never crash on missing/wrong type
- `json_cstr(a, v)` - NUL-terminated copy for printf
- deep chaining: `json_str(json_get(json_get(root,"a"),"b"))` always safe
- emitter: `JsonEmit`, `json_obj_start/end`, `json_arr_start/end`, `json_key`, `json_str_v`, `json_num_i`, `json_bool_v`
- `json_val` - re-emit any parsed subtree
- requires barebuilder

---

**barecsv.h** - CSV / TSV reader

- pull parser: `CsvReader`, `csv_next_row`, `csv_next_field`
- zero-copy for unquoted fields (views into source buffer)
- RFC 4180 quoting with `""` escape handling (arena for unescaped copies)
- configurable delimiter (TSV: `cfg.delimiter = '\t'`) and quote char
- `csv_read_all` - full materialisation into `Slice(Slice(Str))`
- `csv_read_header` - extract header row as `Slice(Str)`
- `has_header`, `trim_spaces` options

---

**bareencoding.h** - base64, hex, percent, HTML

- base64: encode (standard / URL-safe) and decode, both alphabets accepted
- hex: `hex_encode` / `hex_encode_upper` / `hex_decode`, accepts `0x` prefix
- percent (URI, RFC 3986): `pct_encode`, `pct_encode_path`, `pct_encode_query`, `pct_decode`
- HTML: `html_escape` / `html_unescape` with `&#N;` numeric entities

---

### testing / observability

**baretest.h** - minimal TAP test runner

- `TEST("name") { }` blocks with per-test timing (requires baretime)
- `ASSERT`, `ASSERT_EQ`, `ASSERT_NE`, `ASSERT_LT/LE/GT/GE`
- `ASSERT_STR_EQ`, `ASSERT_STR`, `ASSERT_NULL`, `ASSERT_NOT_NULL`
- `ASSERT_NEAR(a, b, eps)`, `ASSERT_MEM_EQ(p, q, n)`
- `SKIP("reason")` - TAP `# SKIP` annotation
- outputs TAP 13, returns 0 or 1

```c
TEST("basic") { ASSERT(1 + 1 == 2); }
TEST("not ready") { SKIP("needs network"); }
return baretest_finish();
```

---

**barelog.h** - structured logging

- levels: `LOG_DEBUG` / `LOG_INFO` / `LOG_WARN` / `LOG_ERROR` / `LOG_NONE`
- `log_set_sink_file`, `log_set_sink_writer`, `log_set_sink_fn`
- `log_with(key, val)` / `log_with_int(key, n)` - structured fields on next call
- zero allocation on hot path (static 1 KB format buffer)
- thread-safe via `log_set_mutex` (requires baresync)
- requires baretime

```c
log_set_level(LOG_INFO);
log_with("user", str_lit("alice"));
log_info("http", "GET /api/users 200");
// -> 2025-03-21 14:32:00 INFO  [http] GET /api/users 200 user=alice
```

---

## install

**single file:**
```bash
curl -fsSL https://raw.githubusercontent.com/Salvadego/bared/dev/barestd.h -o barestd.h
```

**multiple files:**
```bash
curl -fsSL https://raw.githubusercontent.com/Salvadego/bared/dev/install.sh | bash -s -- core
```

or download the script and run it yourself:
```bash
curl -fsSL https://raw.githubusercontent.com/Salvadego/bared/dev/install.sh -o bare
chmod +x bare
./bare all
```

**options:**

| command                        | what you get                                          |
| ---                            | ---                                                   |
| `bare all`                     | every header                                          |
| `bare core`                    | barestd, baretime, bareio, barestrs, bareutf8, bareos |
| `bare threads`                 | barestd, baresync, barethreads, barepool              |
| `bare net`                     | barestd, baretime, barenet, baresync, barethreads     |
| `bare web`                     | net + barebuilder, barejson, bareencoding, barehttp   |
| `bare barestd.h barenet.h`     | specific files                                        |

**install to a different directory:**
```bash
DEST=include bare core
```

**pin to a specific version:**
```bash
VERSION=v1.0.0 bare all
```

or just copy the headers you need directly. they have no build step.

---

## important notes

### this is NOT allocation-free

there **are allocations**, just not hidden behind `malloc`:

- arena growth (transparent overflow chaining)
- scratch allocations (freed on `scratch_end`)
- slice/builder growth
- temporary internal buffers

you control *where*, not *if*.

---

### scratch memory is temporary

```c
Str s = sb_to_str(&sb);   /* points into scratch */
sb_reset(&sb);            /* scratch freed -- s is now invalid */
```

use `sb_to_cstr(a, &sb)` to get an arena-stable copy.

---

### no error handling

```c
assert(...)
```

failure = crash. design your programs so invalid states don't happen.

---

### not thread-safe by default

arenas, slices, and maps are not thread-safe. use `baresync.h` to wrap
access, or give each thread its own arena.

---

### some requirements

- power-of-two sizes for buffers and pools
- POSIX or Windows
- C99 compiler (GCC or Clang recommended; MSVC works with minor limitations)

---

## roadmap

### medium-term

**baredns.h** - async DNS resolver

**barearena improvements**

- virtual memory backend (mmap/VirtualAlloc) for large arenas with no copying on overflow
- arena statistics / watermark tracking

---

### long-term / speculative

**barehttp improvements**

- chunked transfer encoding
- TLS via a pluggable backend
- HTTP/2 framing

---

## tl;dr

- simple
- fast
- manual
- sharp edges

use it if that's what you want.
