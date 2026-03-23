# bare - small C99 utility headers

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

| module                | flag                      |
| ---                   | ---                       |
| baresync, barethreads | `-lpthread` (Linux/macOS) |
| baremath              | `-lm`                     |
| barenet               | `-lws2_32` (Windows only) |

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
- `buf_read_line` for line-by-line reading
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
- static 128-byte error buffer, hot paths allocate nothing

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

| command                    | what you get                                          |
| ---                        | ---                                                   |
| `bare all`                 | every header                                          |
| `bare core`                | barestd, baretime, bareio, barestrs, bareutf8, bareos |
| `bare threads`             | barestd, baresync, barethreads, barepool              |
| `bare barestd.h barenet.h` | specific files                                        |

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

**baretest.h** - minimal test runner

```c
TEST("basic") { ASSERT(1 + 1 == 2); }
```

no dependencies, outputs TAP or simple pass/fail, counts failures.

**barelog.h** - structured logging

- levels: DEBUG / INFO / WARN / ERROR
- timestamped lines via baretime
- sink abstraction (stderr, file, custom writer)
- zero allocation on hot path (static format buffer)

---

### medium-term

**barejson.h** - pull parser

- event-based, zero allocation during parse
- arena for string values
- no schema, no codegen

**barecsv.h** - CSV / TSV reader

- field iteration, no full materialisation
- configurable delimiter and quoting

**barenet improvements**

- `net_select` / `net_poll` multiplexing helpers
- connection pool over barepool
- address iteration for multi-address DNS results

---

### long-term / speculative

**baredns.h** - async DNS resolver

**barehttp.h** - minimal HTTP/1.1 client (request/response framing over barenet)

**barearena improvements**
    - virtual memory backend (mmap/VirtualAlloc) for large arenas with no copying on overflow
    - arena statistics / watermark tracking

---

## tl;dr

- simple
- fast
- manual
- sharp edges

use it if that's what you want.
