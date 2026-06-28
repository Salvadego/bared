/*
 * baredefs.h - foundation macros and assertions
 * =================================================
 *
 *  USAGE
 *    #include "baredefs.h"
 *
 *  This header has no implementation section; it is pure preprocessor
 *  and safe to include from anywhere, any number of times, with no
 *  build-system consequences.
 *
 *  SCOPE
 *    Arithmetic and sizing helpers (KB/MB/GB, AlignUp, IsPow2, Min/Max),
 *    the BAREDEF linkage macro, and the assertion vocabulary every
 *    other "bare" header is written against. Nothing here depends on
 *    an operating system or a specific compiler; see barecompilers.h
 *    and bareplatform.h for those.
 *
 *  ASSERTIONS AS A DESIGN TOOL, NOT JUST A SAFETY NET
 *    The assertion discipline used throughout this codebase has one
 *    job: turn a silent, distant memory-corruption bug into a loud,
 *    immediate, precisely-located crash, as close as possible to the
 *    line that actually went wrong. This idea is much older than any
 *    one style guide -- it is the whole premise of Gerard Holzmann's
 *    work on defensive coding for safety-critical systems (see his
 *    "Power of Ten" rules for spacecraft flight software, and the SPIN
 *    model checker's reliance on assertions as machine-checkable
 *    specification) and of the long Unix and kernel tradition of
 *    "fail fast, fail loud, fail at the boundary." A function that
 *    blindly trusts its inputs and silently produces a wrong answer
 *    is strictly worse than one that crashes the moment something it
 *    depends on stops being true: a crash has a stack trace pointing
 *    at the actual bug, and a wrong answer has neither.
 *
 *    Concretely, that means asserting BOTH halves of every meaningful
 *    boundary, not just one:
 *      - the positive space: what must be true for the function to do
 *        anything sensible (preconditions on its arguments, and
 *        postconditions on what it hands back);
 *      - the negative space: what must NEVER be true (the invalid
 *        states a caller's bug, or corrupted data, could put the
 *        program into). The interesting bugs live at the boundary
 *        between these two spaces, which is exactly why both sides
 *        need their own check rather than one combined "looks fine"
 *        condition: a single Assert(a && b) tells you something
 *        failed; two separate Assert(a) and Assert(b) tell you which.
 *
 *    This is why AssertPositive and AssertNegative exist as distinct
 *    names below even though they expand to the same underlying
 *    check: naming the side of the boundary you are guarding is, on
 *    its own, useful documentation at the call site.
 */
#ifndef BAREDEFS_H
#define BAREDEFS_H

#include <stdio.h>
#include <stdlib.h>

/*
 * BAREDEF - linkage for every public function in the "bare" headers
 *
 * Defining BARE_STATIC before including a "bare" header confines
 * every BAREDEF-marked declaration in it to `static` linkage, for a
 * single-translation-unit / unity-build use. Left undefined, BAREDEF
 * is plain `extern` linkage, the default for any top-level C function,
 * letting the implementation live in one .c file and be called from
 * others in the ordinary way.
 */
#if defined(BARE_STATIC)
#        define BAREDEF static
#else
#        define BAREDEF extern
#endif

/* ----------------------------------------------------------------
 *  Sizing and arithmetic
 * ---------------------------------------------------------------- */

/* OffsetOfMember - byte offset of member @m within struct type @T */
#define OffsetOfMember(T, m) offsetof(T, m)

/* AlignOfType - the alignment requirement, in bytes, of type @T */
#define AlignOfType(T)   \
        (sizeof(struct { \
                 char c; \
                 T    x; \
         }) -            \
         sizeof(T))

/* ArrayCount - number of elements in the fixed-size array @a (not a pointer) */
#define ArrayCount(a) (sizeof(a) / sizeof(*(a)))

/* KB / MB / GB - byte counts: KB(4) is 4096, MB(1) is 1048576, and so on */
#define KB(n) ((size_t)(n) * (size_t)1024)
#define MB(n) ((size_t)(n) * (size_t)1024 * 1024)
#define GB(n) ((size_t)(n) * (size_t)1024 * 1024 * 1024)

/* AlignUp - round @n up to the next multiple of @a; @a must be a power of two */
#define AlignUp(n, a) (((size_t)(n) + (size_t)(a) - 1) & ~((size_t)(a) - 1))

/* AlignDown - round @n down to the previous multiple of @a; @a must be a power of two */
#define AlignDown(n, a) ((size_t)(n) & ~((size_t)(a) - 1))

/* IsPow2 - true if @n is a power of two; false for 0 */
#define IsPow2(n) ((n) != 0 && !((n) & ((n) - 1)))

/* Min / Max - the smaller or larger of @a and @b; each argument is evaluated twice */
#define Min(a, b) ((a) < (b) ? (a) : (b))
#define Max(a, b) ((a) > (b) ? (a) : (b))

/* Clamp - @x restricted to the closed range [@lo, @hi] */
#define Clamp(lo, x, hi) (Max((lo), Min((x), (hi))))

/* Unused - silence an unused-variable/parameter warning for @x */
#define Unused(x) ((void)(x))

/*
 * Put an explicit upper bound on every loop or queue whose trip count
 * is not simply "iterate this caller-supplied array once": a loop
 * that should terminate but is only ASSUMED to, rather than checked
 * to, is a liveness bug waiting to become a hang. BARE_MAX_LOOP_BOUND
 * is the default ceiling AssertBounded (below) checks against when no
 * more specific limit applies; individual modules may define and use
 * their own, tighter limits instead.
 */
#ifndef BARE_MAX_LOOP_BOUND
#        define BARE_MAX_LOOP_BOUND 1000000000u
#endif

/*
 * Override AssertBreak before including this header to change what
 * happens when an assertion fails -- e.g. `raise(SIGTRAP)` to drop
 * straight into a debugger instead of exiting.
 */
#ifndef AssertBreak
#        define AssertBreak() exit(1)
#endif

#ifndef Statement
#        define Statement(s) do { s } while (0)
#endif

/*
 * AssertFailed - report a failed assertion and invoke AssertBreak()
 * @expr: the condition's source text, for the printed message
 * @msg: the human-readable explanation passed to Assert()
 * @file: __FILE__ at the call site
 * @line: __LINE__ at the call site
 *
 * Not normally called directly; Assert() calls this for you. Override
 * by defining your own AssertFailed before including this header if
 * you want a different report format (e.g. structured logging instead
 * of stderr).
 */
#ifndef AssertFailed
#define AssertFailed(expr, msg, file, line)  \
        Statement(fprintf(stderr,            \
                          "%s:%d: [%s] %s\n",\
                          file,              \
                          line,              \
                          msg,               \
                          expr);             \
                          fflush(stderr);    \
                          AssertBreak();)
#endif

/*
 * Assert is the one primitive every other macro below is built from.
 * Compound conditions are deliberately not combined at the call site
 * (prefer two calls, Assert(a, ...); Assert(b, ...);, over one
 * Assert(a && b, ...)) so that a failure message points at exactly
 * which half went wrong, not just that "something" did.
 */
/*
 * Assert - the one primitive every other assertion macro is built from
 * @cond: the condition that must be true
 * @msg: a human-readable explanation, printed if @cond is false
 *
 * Calls AssertFailed() (and therefore AssertBreak(), exit(1) by
 * default) if @cond is false; otherwise does nothing. Compound
 * conditions are deliberately not combined at the call site (prefer
 * two calls, Assert(a, ...); Assert(b, ...);, over one
 * Assert(a && b, ...)) so that a failure message points at exactly
 * which half went wrong, not just that "something" did.
 */
#ifndef Assert
#define Assert(cond, msg)                                       \
        Statement(if (!(cond)) {                                \
                  AssertFailed(#cond, msg, __FILE__, __LINE__); \
                  })
#endif

/* ----------------------------------------------------------------
 *  Paired positive/negative-space assertions
 *
 *  See the file header above for the rationale; mechanically these
 *  are all Assert() under a name that documents intent.
 * ---------------------------------------------------------------- */

/* AssertPositive - assert @cond, naming it as a precondition/postcondition you expect */
#define AssertPositive(cond, msg) Assert((cond), (msg))

/* AssertNegative - assert !@cond, naming it as an invalid state that must never occur */
#define AssertNegative(cond, msg) Assert(!(cond), (msg))

/* AssertImplies - assert @b, but only check it when @a is true ("if (a) Assert(b)") */
#define AssertImplies(a, b, msg) Statement(if (a) { Assert((b), (msg)); })

/* AssertEqual / AssertNotEqual - assert @a == @b or @a != @b */
#define AssertEqual(a, b, msg)    Assert((a) == (b), (msg))
#define AssertNotEqual(a, b, msg) Assert((a) != (b), (msg))

/*
 * AssertBounded states the "this loop terminates" invariant explicitly
 * for any loop whose trip count is itself an assumption rather than a
 * directly-bounded input (a caller-supplied array length, say). Call
 * it once per iteration, before the work that iteration does.
 */
#define AssertBounded(count, max, msg) \
        Assert((size_t)(count) <= (size_t)(max), (msg))

/*
 * A compile-time assertion needs the C11 _Static_assert or, on C99, a
 * portable array-size trick; both are provided by barecompilers.h as
 * BARE_STATIC_ASSERT. StaticAssert is kept here, spelled the C99 way
 * directly, only for translation units that include baredefs.h without
 * barecompilers.h and don't need the C11 path; new code should prefer
 * BARE_STATIC_ASSERT.
 */
#define StaticAssert(expr) typedef char _baredefs_sa_##__LINE__[(expr) ? 1 : -1]

#endif /* BAREDEFS_H */
