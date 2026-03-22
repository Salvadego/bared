# bare — small C99 utility headers

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

    char *s = sb_to_cstr(a, &sb);
}
```

---

## modules

### core

**barestd.h**
core stuff everything depends on
* arena allocator
* slices
* strings (`Str`)
* scratch memory
* helpers/macros

---

**barestrs.h**
string helpers
* comparisons
* slicing
* formatting helpers

---

**bareutf8.h**
utf-8 utilities
* rune handling
* encoding/decoding
* width/count

---

## data structures

**barebuf.h**
ring buffer
* power-of-two capacity
* arena or stack version
* no resizing

---

**barepool.h**
object pool
* fixed-size allocations
* fast reuse
* no free (just recycle)

---

## building / formatting

**barebuilder.h**
string builder
* scratch-backed
* fast append
* finalize → arena copy

---

## io / system

**bareio.h**
reader / writer abstraction
* FILE*
* memory
* optional OS backend

---

**bareos.h**
OS layer
* files
* basic platform stuff

---

**barepath.h**
path utilities
* join / split
* normalize
* extensions

---

## concurrency

**baresync.h**
low-level sync primitives
* mutex

---

**barethreads.h**
threading
* thread create/join
* minimal wrapper over OS

---

## processes / signals

**bareproc.h**
process handling
* spawn processes
* basic control

---

**baresig.h**
signal handling
* install handlers
* basic signal utils

---

## networking

**barenet.h**
networking
* sockets
* basic TCP/UDP helpers

---

## time

**baretime.h**
time utilities
* timestamps
* sleep
* conversions

---

## math

**baremath.h**
math + extras
* wraps `<math.h>`
* adds:
  * vectors
  * bit ops
  * fixed point
  * helpers

---

## important notes

### this is NOT allocation-free
there **are allocations**, just not hidden behind malloc calls:

* arena growth
* scratch allocations
* buffer growth (e.g. IO, builders)
* temporary internal buffers

you control *where*, not *if*.

---

### scratch memory is temporary
this is invalid after reset/finalize:

```c
Str s = sb_to_str(&sb);
```

don’t keep it.

---

### no error handling
everything uses:

```c
assert(...)
```

failure = crash.

---

### not thread-safe (by default)
you handle sync yourself.

---

### some requirements
* power-of-two sizes (buffers, pools)
* sane platform (POSIX / Windows)
* C99 compiler

---

## tl;dr
* simple
* fast
* manual
* sharp edges

use it if that’s what you want.
