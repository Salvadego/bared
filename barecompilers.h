/*
 * barecompilers.h - compiler and language-standard detection
 * =============================================================
 *
 *  USAGE
 *    #include "barecompilers.h"
 *
 *  This header has no implementation section and nothing to define in
 *  any one translation unit; it is pure macros, safe to include from
 *  anywhere, any number of times.
 *
 *  SCOPE
 *    This file answers exactly two questions: "which compiler is this"
 *    and "which C standard is this", and then provides macros that let
 *    the rest of the codebase stop asking either question directly.
 *    It knows nothing about operating systems; see bareplatform.h for
 *    that. Every other "bare" header should depend on this file (and
 *    bareplatform.h) instead of checking __GNUC__, _MSC_VER, or
 *    __STDC_VERSION__ on its own -- one place to get a feature shim
 *    right is easier to keep right than twenty.
 *
 *  GUARANTEE
 *    Every macro below compiles under all of:
 *      gcc   -std=c99 -pedantic-errors
 *      clang -std=c99 -pedantic-errors
 *      MSVC  /std:c11 (MSVC has no conforming C99 mode; see BARE_COMPILER_MSVC
 *            notes below for what that means in practice)
 *    with no GNU/Clang/MSVC extension required to use the macro itself.
 *    Where the underlying C99 standard already provides the feature
 *    (e.g. `inline`, `restrict`), the macro is just that keyword,
 *    spelled once, so code that uses BARE_INLINE instead of `inline`
 *    is not "less standard" -- it is the same code, plus automatic
 *    correctness on MSVC, which spells some of these differently.
 *
 *  WHY THIS EXISTS AT ALL
 *    A small number of things that every nontrivial C codebase needs
 *    cannot be spelled identically across MSVC / GCC / Clang and
 *    across C99 / C11 without a shim, because the C99 standard itself
 *    has no answer for them (thread-local storage, for one) or because
 *    MSVC's pre-C11 dialect spells C99 features non-conformingly even
 *    though it accepts them (e.g. `inline` was a Microsoft extension
 *    before MSVC's belated C11 support). Centralizing the eleven or so
 *    macros below is the entire difference between "portable C" and
 *    "C that happens to work on whichever compiler the author had
 *    open."
 */
#ifndef BARECOMPILERS_H
#define BARECOMPILERS_H

/* ----------------------------------------------------------------
 *  Compiler identification
 *
 *  Checked in this order on purpose: clang-cl and other clang-based
 *  toolchains define _MSC_VER for MSVC compatibility, so clang must
 *  be detected before MSVC, or every clang invocation would be
 *  misidentified as "real" MSVC.
 * ---------------------------------------------------------------- */
#if defined(__clang__)
#        define BARE_COMPILER_CLANG 1
#elif defined(__GNUC__)
#        define BARE_COMPILER_GCC 1
#elif defined(_MSC_VER)
#        define BARE_COMPILER_MSVC 1
#else
#        define BARE_COMPILER_UNKNOWN 1
#endif

#ifndef BARE_COMPILER_CLANG
#        define BARE_COMPILER_CLANG 0
#endif
#ifndef BARE_COMPILER_GCC
#        define BARE_COMPILER_GCC 0
#endif
#ifndef BARE_COMPILER_MSVC
#        define BARE_COMPILER_MSVC 0
#endif
#ifndef BARE_COMPILER_UNKNOWN
#        define BARE_COMPILER_UNKNOWN 0
#endif

/* ----------------------------------------------------------------
 *  Language standard identification
 *
 *  MSVC does not set __STDC_VERSION__ to a useful value unless built
 *  with /std:c11 or newer and /Zc:__STDC__ besides; treat any MSVC
 *  build as "at least C11" for the purposes of this header, since
 *  every MSVC version still receiving updates supports the C11
 *  features this header cares about (_Thread_local in particular is
 *  the awkward one, and is covered by its own fallback below either
 *  way).
 * ---------------------------------------------------------------- */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#        define BARE_STDC_C11 1
#else
#        define BARE_STDC_C11 0
#endif

/* ----------------------------------------------------------------
 *  BARE_INLINE
 *
 *  `inline` is standard C99. The only reason this macro exists is
 *  that pre-2015 MSVC required `__inline` instead; current MSVC
 *  accepts plain `inline` in C mode, but the spelling below costs
 *  nothing and removes the question for any older toolchain a game
 *  studio might still be building with.
 * ---------------------------------------------------------------- */
#if BARE_COMPILER_MSVC
#        define BARE_INLINE __inline
#else
#        define BARE_INLINE inline
#endif

/* ----------------------------------------------------------------
 *  BARE_RESTRICT
 *
 *  `restrict` is standard C99. MSVC's C99 support has never included
 *  the `restrict` keyword under that spelling; `__restrict` is its
 *  equivalent and is accepted in both its C and C++ modes.
 * ---------------------------------------------------------------- */
#if BARE_COMPILER_MSVC
#        define BARE_RESTRICT __restrict
#else
#        define BARE_RESTRICT restrict
#endif

/* ----------------------------------------------------------------
 *  BARE_THREAD_LOCAL
 *
 *  There is no thread-local storage keyword in C99 at all; this is
 *  not a portability gap this header can paper over with a clever
 *  macro, it is an absence in the standard itself. _Thread_local
 *  arrived in C11. Every compiler in practice has offered some
 *  pre-C11 spelling: GCC/Clang's __thread, MSVC's __declspec(thread).
 *  This macro picks the right one and is the single place that
 *  decision is made for the whole codebase.
 * ---------------------------------------------------------------- */
#if BARE_STDC_C11 && !BARE_COMPILER_MSVC
#        define BARE_THREAD_LOCAL _Thread_local
#elif BARE_COMPILER_MSVC
#        define BARE_THREAD_LOCAL __declspec(thread)
#elif BARE_COMPILER_GCC || BARE_COMPILER_CLANG
#        define BARE_THREAD_LOCAL __thread
#else
#        define BARE_THREAD_LOCAL /* single-threaded fallback: no-op */
#        define BARE_NO_THREAD_LOCAL 1
#endif

/* ----------------------------------------------------------------
 *  BARE_STATIC_ASSERT(condition, message)
 *
 *  _Static_assert is C11. The portable C99 fallback is the classic
 *  "negative array size is a compile error" trick: a typedef'd array
 *  of size 1 on success and an illegal negative size on failure. The
 *  message argument is accepted but unused in the C99 path (C99 has
 *  no facility to surface a custom message at compile time at all);
 *  it is kept as a parameter purely so call sites read the same way
 *  regardless of which path compiles.
 * ---------------------------------------------------------------- */
#if BARE_STDC_C11
#        define BARE_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
#        define BARE_STATIC_ASSERT_GLUE2(a, b) a##b
#        define BARE_STATIC_ASSERT_GLUE(a, b)  BARE_STATIC_ASSERT_GLUE2(a, b)
#        define BARE_STATIC_ASSERT(cond, msg)                       \
                typedef char BARE_STATIC_ASSERT_GLUE(                \
                    bare_static_assert_, __LINE__)[(cond) ? 1 : -1]
#endif

/* ----------------------------------------------------------------
 *  BARE_NORETURN
 *
 *  _Noreturn is C11. GCC/Clang have always accepted the attribute
 *  spelling in C99 mode; MSVC has its own declspec. Functions that
 *  use this should still also assert(0) or similarly fail loudly
 *  inside the function body -- this macro is an optimizer hint and a
 *  reader hint, not a correctness mechanism on its own.
 * ---------------------------------------------------------------- */
#if BARE_STDC_C11
#        define BARE_NORETURN _Noreturn
#elif BARE_COMPILER_GCC || BARE_COMPILER_CLANG
#        define BARE_NORETURN __attribute__((noreturn))
#elif BARE_COMPILER_MSVC
#        define BARE_NORETURN __declspec(noreturn)
#else
#        define BARE_NORETURN
#endif

/* ----------------------------------------------------------------
 *  BARE_UNREACHABLE()
 *
 *  Marks a code path the author asserts can never execute. This is
 *  an optimizer hint, not a check: it must only ever be placed after
 *  an explicit assertion failure (e.g. a `default` switch case that
 *  already called an assert-and-abort helper), never as a substitute
 *  for one, or a real bug becomes silently undefined behavior instead
 *  of a loud crash.
 * ---------------------------------------------------------------- */
#if BARE_COMPILER_GCC || BARE_COMPILER_CLANG
#        define BARE_UNREACHABLE() __builtin_unreachable()
#elif BARE_COMPILER_MSVC
#        define BARE_UNREACHABLE() __assume(0)
#else
#        define BARE_UNREACHABLE() ((void)0)
#endif

/* ----------------------------------------------------------------
 *  BARE_LIKELY(x) / BARE_UNLIKELY(x)
 *
 *  Branch-probability hints for the optimizer; on a compiler that
 *  does not support them they are exactly `(x)`, so behavior is
 *  always identical and only codegen quality changes.
 * ---------------------------------------------------------------- */
#if BARE_COMPILER_GCC || BARE_COMPILER_CLANG
#        define BARE_LIKELY(x)   (__builtin_expect(!!(x), 1))
#        define BARE_UNLIKELY(x) (__builtin_expect(!!(x), 0))
#else
#        define BARE_LIKELY(x)   (x)
#        define BARE_UNLIKELY(x) (x)
#endif

/* ----------------------------------------------------------------
 *  BARE_ALIGNAS(n)
 *
 *  _Alignas is C11. GCC/Clang's __attribute__((aligned(n))) and
 *  MSVC's __declspec(align(n)) both predate it and remain available.
 * ---------------------------------------------------------------- */
#if BARE_STDC_C11
#        define BARE_ALIGNAS(n) _Alignas(n)
#elif BARE_COMPILER_GCC || BARE_COMPILER_CLANG
#        define BARE_ALIGNAS(n) __attribute__((aligned(n)))
#elif BARE_COMPILER_MSVC
#        define BARE_ALIGNAS(n) __declspec(align(n))
#else
#        define BARE_ALIGNAS(n) /* alignment hint unavailable */
#endif

#endif /* BARECOMPILERS_H */
