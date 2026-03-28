/*
 * baretest.h -- TAP 13 test runner
 * ==================================
 *
 *  USAGE
 *    #define BARETEST_IMPLEMENTATION
 *    #include "baretest.h"
 *
 *  DEPENDS ON
 *    barestd.h, baretime.h  (include baretime.h first for timing)
 *
 *  DESIGN
 *    Each TEST("name") { } block is one TAP test case.
 *    Assertions inside are counted as checks; any failure marks the case
 * failed. Sub-tests nest with SUBTEST("name") { }. Timing per test is reported
 * if baretime.h was included. SKIP("reason") inside a TEST() skips it.
 *    baretest_finish() prints summary and returns 0 (all pass) or 1.
 *
 *  OUTPUT
 *    TAP version 13
 *    1..4
 *    ok 1 - addition  (0.12ms)
 *    not ok 2 - subtraction
 *      ---
 *      message: 3 != 4
 *      at: test_foo.c:17
 *      ...
 *    ok 3 # SKIP known bug
 *    ok 4 - strings
 *    # 3/4 passed, 1 failed, 1 skipped  (5 checks)
 *
 *  ASSERTIONS
 *    ASSERT(expr)
 *    ASSERT_EQ(type, a, b)     -- a == b
 *    ASSERT_NE(type, a, b)     -- a != b
 *    ASSERT_LT(type, a, b)     -- a < b
 *    ASSERT_LE(type, a, b)     -- a <= b
 *    ASSERT_GT(type, a, b)     -- a > b
 *    ASSERT_GE(type, a, b)     -- a >= b
 *    ASSERT_STR_EQ(a, b)       -- strcmp == 0
 *    ASSERT_STR(a, b)          -- str_eq(a, b)
 *    ASSERT_NULL(p)            -- p == NULL
 *    ASSERT_NOT_NULL(p)        -- p != NULL
 *    ASSERT_NEAR(a, b, eps)    -- |a-b| <= eps
 *    ASSERT_MEM_EQ(p, q, n)   -- memcmp == 0
 *
 *  EXAMPLE
 *    TEST("math") {
 *        ASSERT_EQ(int, 1+1, 2);
 *        ASSERT_NEAR(3.14159, MATH_PI, 0.001);
 *    }
 *    TEST("strings") {
 *        SKIP("not implemented yet");
 *    }
 *    return baretest_finish();
 */
#ifndef BARETEST_H
#define BARETEST_H

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "barestd.h"

#ifdef BARETIME_H
#        define _BT_HAVE_TIME 1
#else
#        define _BT_HAVE_TIME 0
#endif

/* ================================================================
 *  State (defined in IMPLEMENTATION)
 * ================================================================ */
extern int         _bt_checks;
extern int         _bt_failures;
extern int         _bt_case_failed;
extern int         _bt_case_skipped;
extern const char* _bt_case_name;
extern const char* _bt_skip_reason;

#if _BT_HAVE_TIME
extern Instant _bt_case_start;
#endif

/* ================================================================
 *  TEST block
 * ================================================================ */
#define TEST(name_str) \
        for (_baretest_begin(name_str); _bt_case_name != NULL; _baretest_end())

/* ================================================================
 *  SKIP — call inside TEST() to mark skipped, early-exit block
 * ================================================================ */
#define SKIP(reason)                         \
        do {                                 \
                _bt_case_skipped = 1;        \
                _bt_skip_reason  = (reason); \
                break;                       \
        } while (0)

/* ================================================================
 *  SUBTEST — nested named group (counts as a check)
 * ================================================================ */
#define SUBTEST(name_str)                                           \
        for (printf("    # subtest: %s\n", name_str), _bt_checks++; \
             _bt_checks > 0;                                        \
             _bt_checks -= 0, (void)0)

/* ================================================================
 *  Assertion macros
 * ================================================================ */
#define ASSERT(expr)                                                         \
        do {                                                                 \
                _bt_checks++;                                                \
                if (!(expr)) {                                               \
                        _bt_failures++;                                      \
                        _bt_case_failed = 1;                                 \
                        fprintf(stderr,                                      \
                                "  ---\n  message: assertion failed: %s\n  " \
                                "at: %s:%d\n  ...\n",                        \
                                #expr,                                       \
                                __FILE__,                                    \
                                __LINE__);                                   \
                }                                                            \
        } while (0)

#define _ASSERT_OP(type, a, b, op, op_str)                                  \
        do {                                                                \
                _bt_checks++;                                               \
                type _bt_a = (a);                                           \
                type _bt_b = (b);                                           \
                if (!(_bt_a op _bt_b)) {                                    \
                        _bt_failures++;                                     \
                        _bt_case_failed = 1;                                \
                        fprintf(stderr,                                     \
                                "  ---\n  message: (%s) %s (%s) failed at " \
                                "%s:%d\n  ...\n",                           \
                                #a,                                         \
                                op_str,                                     \
                                #b,                                         \
                                __FILE__,                                   \
                                __LINE__);                                  \
                }                                                           \
        } while (0)

#define ASSERT_EQ(type, a, b) _ASSERT_OP(type, a, b, ==, "==")
#define ASSERT_NE(type, a, b) _ASSERT_OP(type, a, b, !=, "!=")
#define ASSERT_LT(type, a, b) _ASSERT_OP(type, a, b, <, "<")
#define ASSERT_LE(type, a, b) _ASSERT_OP(type, a, b, <=, "<=")
#define ASSERT_GT(type, a, b) _ASSERT_OP(type, a, b, >, ">")
#define ASSERT_GE(type, a, b) _ASSERT_OP(type, a, b, >=, ">=")

#define ASSERT_STR_EQ(a, b)                                              \
        do {                                                             \
                _bt_checks++;                                            \
                const char* _bt_a = (a);                                 \
                const char* _bt_b = (b);                                 \
                if (strcmp(_bt_a, _bt_b) != 0) {                         \
                        _bt_failures++;                                  \
                        _bt_case_failed = 1;                             \
                        fprintf(stderr,                                  \
                                "  ---\n  message: \"%s\" != \"%s\" at " \
                                "%s:%d\n  ...\n",                        \
                                _bt_a,                                   \
                                _bt_b,                                   \
                                __FILE__,                                \
                                __LINE__);                               \
                }                                                        \
        } while (0)

#define ASSERT_STR(a, b)                                                  \
        do {                                                              \
                _bt_checks++;                                             \
                Str _bt_a = (a);                                          \
                Str _bt_b = (b);                                          \
                if (!str_eq(_bt_a, _bt_b)) {                              \
                        _bt_failures++;                                   \
                        _bt_case_failed = 1;                              \
                        fprintf(stderr,                                   \
                                "  ---\n  message: \"" StrFmt             \
                                "\" != \"" StrFmt "\" at %s:%d\n  ...\n", \
                                StrArgs(_bt_a),                           \
                                StrArgs(_bt_b),                           \
                                __FILE__,                                 \
                                __LINE__);                                \
                }                                                         \
        } while (0)

#define ASSERT_NULL(p)                                                    \
        do {                                                              \
                _bt_checks++;                                             \
                if ((p) != NULL) {                                        \
                        _bt_failures++;                                   \
                        _bt_case_failed = 1;                              \
                        fprintf(stderr,                                   \
                                "  ---\n  message: expected NULL: %s at " \
                                "%s:%d\n  ...\n",                         \
                                #p,                                       \
                                __FILE__,                                 \
                                __LINE__);                                \
                }                                                         \
        } while (0)

#define ASSERT_NOT_NULL(p)                                                    \
        do {                                                                  \
                _bt_checks++;                                                 \
                if ((p) == NULL) {                                            \
                        _bt_failures++;                                       \
                        _bt_case_failed = 1;                                  \
                        fprintf(stderr,                                       \
                                "  ---\n  message: expected non-NULL: %s at " \
                                "%s:%d\n  ...\n",                             \
                                #p,                                           \
                                __FILE__,                                     \
                                __LINE__);                                    \
                }                                                             \
        } while (0)

#define ASSERT_NEAR(a, b, eps)                                              \
        do {                                                                \
                _bt_checks++;                                               \
                double _bt_a = (double)(a), _bt_b = (double)(b),            \
                       _bt_eps  = (double)(eps);                            \
                double _bt_diff = _bt_a - _bt_b;                            \
                if (_bt_diff < 0) _bt_diff = -_bt_diff;                     \
                if (_bt_diff > _bt_eps) {                                   \
                        _bt_failures++;                                     \
                        _bt_case_failed = 1;                                \
                        fprintf(stderr,                                     \
                                "  ---\n  message: |%g - %g| = %g > %g at " \
                                "%s:%d\n  ...\n",                           \
                                _bt_a,                                      \
                                _bt_b,                                      \
                                _bt_diff,                                   \
                                _bt_eps,                                    \
                                __FILE__,                                   \
                                __LINE__);                                  \
                }                                                           \
        } while (0)

#define ASSERT_MEM_EQ(p, q, n)                                                \
        do {                                                                  \
                _bt_checks++;                                                 \
                if (memcmp((p), (q), (n)) != 0) {                             \
                        _bt_failures++;                                       \
                        _bt_case_failed = 1;                                  \
                        fprintf(stderr,                                       \
                                "  ---\n  message: memory differs: %s vs %s " \
                                "(%zu bytes) at %s:%d\n  ...\n",              \
                                #p,                                           \
                                #q,                                           \
                                (size_t)(n),                                  \
                                __FILE__,                                     \
                                __LINE__);                                    \
                }                                                             \
        } while (0)

/* ================================================================
 *  Internal helpers
 * ================================================================ */
void _baretest_begin(const char* name);
void _baretest_end(void);
int  baretest_finish(void);

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#ifdef BARETEST_IMPLEMENTATION

int         _bt_checks       = 0;
int         _bt_failures     = 0;
int         _bt_case_failed  = 0;
int         _bt_case_skipped = 0;
const char* _bt_case_name    = NULL;
const char* _bt_skip_reason  = NULL;

#        if _BT_HAVE_TIME
Instant _bt_case_start;
#        endif

#        define _BT_MAX_CASES 2048
static struct {
        const char* name;
        int         failed;
        int         skipped;
        const char* skip_reason;
#        if _BT_HAVE_TIME
        double ms;
#        endif
} _bt_log[_BT_MAX_CASES];

static int _bt_case_count  = 0;
static int _bt_case_passed = 0;
static int _bt_case_skip_n = 0;

void _baretest_begin(const char* name) {
        _bt_case_name    = name;
        _bt_case_failed  = 0;
        _bt_case_skipped = 0;
        _bt_skip_reason  = NULL;
#        if _BT_HAVE_TIME
        _bt_case_start = instant_now();
#        endif
}

void _baretest_end(void) {
#        if _BT_HAVE_TIME
        double ms = (double)instant_since(_bt_case_start) / 1e6;
#        endif
        if (_bt_case_count < _BT_MAX_CASES) {
                _bt_log[_bt_case_count].name        = _bt_case_name;
                _bt_log[_bt_case_count].failed      = _bt_case_failed;
                _bt_log[_bt_case_count].skipped     = _bt_case_skipped;
                _bt_log[_bt_case_count].skip_reason = _bt_skip_reason;
#        if _BT_HAVE_TIME
                _bt_log[_bt_case_count].ms = ms;
#        endif
        }
        _bt_case_count++;
        if (_bt_case_skipped)
                _bt_case_skip_n++;
        else if (!_bt_case_failed)
                _bt_case_passed++;
        _bt_case_name = NULL;
}

int baretest_finish(void) {
        int i;
        printf("TAP version 13\n");
        printf("1..%d\n", _bt_case_count);
        for (i = 0; i < _bt_case_count && i < _BT_MAX_CASES; i++) {
                const char* status = _bt_log[i].failed ? "not ok" : "ok";
#        if _BT_HAVE_TIME
                if (_bt_log[i].skipped)
                        printf("%s %d - %s # SKIP %s\n",
                               status,
                               i + 1,
                               _bt_log[i].name,
                               _bt_log[i].skip_reason ? _bt_log[i].skip_reason
                                                      : "");
                else
                        printf("%s %d - %s  (%.2fms)\n",
                               status,
                               i + 1,
                               _bt_log[i].name,
                               _bt_log[i].ms);
#        else
                if (_bt_log[i].skipped)
                        printf("%s %d - %s # SKIP %s\n",
                               status,
                               i + 1,
                               _bt_log[i].name,
                               _bt_log[i].skip_reason ? _bt_log[i].skip_reason
                                                      : "");
                else
                        printf("%s %d - %s\n", status, i + 1, _bt_log[i].name);
#        endif
        }
        printf("# %d/%d passed", _bt_case_passed, _bt_case_count);
        if (_bt_case_skip_n) printf(", %d skipped", _bt_case_skip_n);
        int failed_n = _bt_case_count - _bt_case_passed - _bt_case_skip_n;
        if (failed_n) printf(", %d FAILED", failed_n);
        printf("  (%d checks)\n", _bt_checks);
        return (_bt_failures > 0) ? 1 : 0;
}

#endif /* BARETEST_IMPLEMENTATION */
#endif /* BARETEST_H */
