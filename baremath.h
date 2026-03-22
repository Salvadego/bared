/*
 * baremath.h -- Comprehensive math library
 * =========================================
 *
 *  USAGE
 *    #define BAREMATH_IMPLEMENTATION
 *    #include "baremath.h"
 *
 *  DEPENDS ON
 *    barestd.h  (StaticAssert, integer types)
 *    <math.h>   (transcendental functions delegated to libc)
 *
 *  SECTIONS
 *    S0   Constants
 *    S1   Type punning unions  -- F32, F64, with bit-field overlays
 *    S2   Special values       -- NaN, Inf, predicates
 *    S3   Basic float ops      -- abs, ceil, floor, round, trunc, sign, dim
 *    S4   Trigonometric        -- sin, cos, tan, asin, acos, atan, atan2,
 * sincos S5   Hyperbolic           -- sinh, cosh, tanh, asinh, acosh, atanh S6
 * Exponential & log    -- exp, exp2, expm1, log, log2, log10, log1p, logb S7
 * Power & root         -- sqrt, cbrt, pow, pow10, hypot S8   FP utilities --
 * frexp, ldexp, modf, ilogb, nextafter, copysign, remainder, fmod, fma S9
 * Special functions    -- erf, erfc, erfinv, erfcinv, gamma, lgamma, Bessel
 * J0/J1/Jn/Y0/Y1/Yn S10  Integer math         -- gcd, lcm, log2_floor,
 * next_pow2, isqrt, abs S11  Bit operations       -- popcount, clz, ctz, bswap,
 * rol, ror, bit_extract, bit_insert S12  Overflow arithmetic  -- add/sub/mul
 * with overflow detection S13  Fixed-point          -- Q16.16 and Q8.24 types
 * with full arithmetic S14  2D/3D/4D vectors     -- union overlay (named fields
 * + array access) S15  Hashing              -- SipHash-1-3, xxHash32, xxHash64,
 * Murmur3-32
 *
 *  DESIGN NOTES
 *    - All Go math package functions are provided with a math_ prefix.
 *    - Type punning unions are standard C99 -- reading a union member
 *      other than the last written is defined in C (unlike C++).
 *    - Anonymous structs inside unions require C11 or a GCC/Clang extension
 *      (-fms-extensions or __extension__). They are guarded with a check.
 *    - Transcendental functions delegate to <math.h>; the wrappers exist
 *      so the caller never needs to include <math.h> directly.
 *    - Fixed-point types are structs containing a single int32_t raw field
 *      to prevent accidental integer arithmetic on them.
 *    - Bessel functions (J0/J1/Jn/Y0/Y1/Yn) require _XOPEN_SOURCE >= 600.
 *      baremath.h defines this before including <math.h> if not already set.
 */

/* Bessel functions need _XOPEN_SOURCE on glibc */
#if defined(__linux__) && !defined(_XOPEN_SOURCE)
#        define _XOPEN_SOURCE 600
#endif
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#        define _DEFAULT_SOURCE 1
#endif

#ifndef BAREMATH_H
#        define BAREMATH_H

#        include <math.h>
#        include <stdbool.h>
#        include <stddef.h>
#        include <stdint.h>

#        include "barestd.h"

/* ================================================================
 *  S0 -- Constants
 * ================================================================ */

#        define MATH_E 2.71828182845904523536 /* e                          */
#        define MATH_LOG2E \
                1.44269504088896340736 /* log2(e)                    */
#        define MATH_LOG10E \
                0.43429448190325182765          /* log10(e)                   */
#        define MATH_LN2 0.69314718055994530942 /* ln(2) */
#        define MATH_LN10 \
                2.30258509299404568402         /* ln(10)                     */
#        define MATH_PI 3.14159265358979323846 /* pi */
#        define MATH_TAU \
                6.28318530717958647692 /* 2pi                         */
#        define MATH_PI_2 \
                1.57079632679489661923 /* pi/2                        */
#        define MATH_PI_4 \
                0.78539816339744830962 /* pi/4                        */
#        define MATH_1_PI \
                0.31830988618379067154 /* 1/pi                        */
#        define MATH_2_PI \
                0.63661977236758134308 /* 2/pi                        */
#        define MATH_2_SQRTPI \
                1.12837916709551257390 /* 2/sqrtpi                       */
#        define MATH_SQRT2 \
                1.41421356237309504880 /* sqrt2                         */
#        define MATH_SQRT1_2 \
                0.70710678118654752440 /* 1/sqrt2                       */
#        define MATH_PHI 1.61803398874989484820 /* golden ratio */
#        define MATH_SQRT3 \
                1.73205080756887729353 /* sqrt3                         */

#        define MATH_F32_MAX 3.40282346638528859812e+38f
#        define MATH_F32_MIN 1.17549435082228750797e-38f /* smallest normal */
#        define MATH_F32_EPS 1.19209289550781250000e-07f /* machine epsilon */
#        define MATH_F64_MAX 1.79769313486231570815e+308
#        define MATH_F64_MIN 2.22507385850720138309e-308 /* smallest normal */
#        define MATH_F64_EPS 2.22044604925031308085e-16  /* machine epsilon */
#        define MATH_F64_INF (1.0 / 0.0)
#        define MATH_F64_NAN (0.0 / 0.0)

/* ================================================================
 *  S1 -- Type punning unions
 *
 *  F32 and F64 expose the raw bit representation of float/double
 *  via union punning -- fully valid in C99.
 *
 *  The bit-field structs (sign/exponent/mantissa) let you inspect
 *  or manipulate IEEE 754 fields directly without shifts and masks.
 *
 *  Note: bit-field layout within a union is implementation-defined
 *  for endianness, but on all mainstream little-endian platforms
 *  (x86, ARM, RISC-V) the layout below is correct.
 * ================================================================ */

typedef union {
        float    f; /* value access                  */
        uint32_t u; /* raw 32-bit pattern            */
        int32_t  i; /* signed raw (for comparisons)  */
} F32;

typedef union {
        double   f; /* value access                  */
        uint64_t u; /* raw 64-bit pattern            */
        int64_t  i; /* signed raw                    */
} F64;

typedef char _bm_f32_sz[(sizeof(F32) == 4) ? 1 : -1];
typedef char _bm_f64_sz[(sizeof(F64) == 8) ? 1 : -1];

/* Inline constructors */
static inline F32 f32_from_float(float f) {
        F32 r;
        r.f = f;
        return r;
}
static inline F32 f32_from_bits(uint32_t u) {
        F32 r;
        r.u = u;
        return r;
}
static inline F64 f64_from_double(double f) {
        F64 r;
        r.f = f;
        return r;
}
static inline F64 f64_from_bits(uint64_t u) {
        F64 r;
        r.u = u;
        return r;
}

/* Go-compatible: Float32bits / Float32frombits / Float64bits / Float64frombits
 */
static inline uint32_t math_float32bits(float f) {
        F32 r;
        r.f = f;
        return r.u;
}
static inline float math_float32frombits(uint32_t b) {
        F32 r;
        r.u = b;
        return r.f;
}
static inline uint64_t math_float64bits(double f) {
        F64 r;
        r.f = f;
        return r.u;
}
static inline double math_float64frombits(uint64_t b) {
        F64 r;
        r.u = b;
        return r.f;
}

/* IEEE 754 field extraction via bit ops -- no struct needed */
#        define F32_SIGN_BIT  (UINT32_C(0x80000000))
#        define F32_EXP_MASK  (UINT32_C(0x7F800000))
#        define F32_MANT_MASK (UINT32_C(0x007FFFFF))
#        define F32_EXP_BIAS  (127)

#        define F64_SIGN_BIT  (UINT64_C(0x8000000000000000))
#        define F64_EXP_MASK  (UINT64_C(0x7FF0000000000000))
#        define F64_MANT_MASK (UINT64_C(0x000FFFFFFFFFFFFF))
#        define F64_EXP_BIAS  (1023)

/* ================================================================
 *  S2 -- Special values and predicates
 * ================================================================ */

/* Go: NaN() */
static inline double math_nan(void) {
        return MATH_F64_NAN;
}
/* Go: Inf(sign) -- sign > 0 -> +Inf, sign <= 0 -> -Inf */
static inline double math_inf(int sign) {
        return sign > 0 ? MATH_F64_INF : -MATH_F64_INF;
}

/* Go: IsNaN(f) */
static inline bool math_is_nan(double f) {
        uint64_t u = math_float64bits(f);
        return (u & ~F64_SIGN_BIT) > F64_EXP_MASK;
}
/* Go: IsInf(f, sign) -- sign 0 = either, >0 = +inf, <0 = -inf */
static inline bool math_is_inf(double f, int sign) {
        uint64_t u = math_float64bits(f);
        if (sign > 0) return u == F64_EXP_MASK;
        if (sign < 0) return u == (F64_SIGN_BIT | F64_EXP_MASK);
        return (u & ~F64_SIGN_BIT) == F64_EXP_MASK;
}
/* Go: Signbit(x) */
static inline bool math_signbit(double f) {
        return (math_float64bits(f) & F64_SIGN_BIT) != 0;
}

/* ================================================================
 *  S3 -- Basic float ops
 * ================================================================ */

/* Go: Abs(x) */
static inline double math_abs(double x) {
        return fabs(x);
}
static inline float math_absf(float x) {
        return fabsf(x);
}

/* Go: Ceil, Floor, Trunc, Round, RoundToEven */
static inline double math_ceil(double x) {
        return ceil(x);
}
static inline double math_floor(double x) {
        return floor(x);
}
static inline double math_trunc(double x) {
        return trunc(x);
}
static inline double math_round(double x) {
        return round(x);
}
static inline double math_round_to_even(double x) {
        return rint(x);
}

/* Go: Mod(x, y) -- IEEE 754 remainder with same sign as x */
static inline double math_mod(double x, double y) {
        return fmod(x, y);
}

/* Go: Remainder(x, y) -- IEEE 754 remainder (round-to-nearest) */
static inline double math_remainder(double x, double y) {
        return remainder(x, y);
}

/* Go: Modf(f) -- integer and fractional parts */
static inline void math_modf(double f, double* integer, double* fractional) {
        *fractional = modf(f, integer);
}

/* Go: Dim(x, y) -- max(x-y, 0) */
static inline double math_dim(double x, double y) {
        double d = x - y;
        return d > 0.0 ? d : 0.0;
}

/* Go: Max(x, y) / Min(x, y) -- NaN-propagating */
static inline double math_fmax(double x, double y) {
        if (math_is_nan(x) || math_is_nan(y)) return MATH_F64_NAN;
        return x > y ? x : y;
}
static inline double math_fmin(double x, double y) {
        if (math_is_nan(x) || math_is_nan(y)) return MATH_F64_NAN;
        return x < y ? x : y;
}

/* Go: Copysign(f, sign) */
static inline double math_copysign(double f, double sign) {
        return copysign(f, sign);
}

/* Clamp a value to [lo, hi] */
static inline double math_clampd(double x, double lo, double hi) {
        return x < lo ? lo : (x > hi ? hi : x);
}
static inline float math_clampf(float x, float lo, float hi) {
        return x < lo ? lo : (x > hi ? hi : x);
}
static inline int math_clampi(int x, int lo, int hi) {
        return x < lo ? lo : (x > hi ? hi : x);
}

/* Linear interpolation: a + t*(b-a), t in [0,1] */
static inline double math_lerp(double a, double b, double t) {
        return a + t * (b - a);
}
static inline float math_lerpf(float a, float b, float t) {
        return a + t * (b - a);
}

/* Inverse lerp: t such that lerp(a,b,t) == v */
static inline double math_inv_lerp(double a, double b, double v) {
        return (v - a) / (b - a);
}

/* Remap value from [in_lo,in_hi] to [out_lo,out_hi] */
static inline double math_remap(
    double v, double in_lo, double in_hi, double out_lo, double out_hi) {
        return out_lo + (v - in_lo) / (in_hi - in_lo) * (out_hi - out_lo);
}

/* Smooth step (Ken Perlin) */
static inline double math_smoothstep(double edge0, double edge1, double x) {
        double t = math_clampd((x - edge0) / (edge1 - edge0), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
}
static inline double math_smootherstep(double edge0, double edge1, double x) {
        double t = math_clampd((x - edge0) / (edge1 - edge0), 0.0, 1.0);
        return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

/* ================================================================
 *  S4 -- Trigonometric
 * ================================================================ */

static inline double math_sin(double x) {
        return sin(x);
}
static inline double math_cos(double x) {
        return cos(x);
}
static inline double math_tan(double x) {
        return tan(x);
}
static inline double math_asin(double x) {
        return asin(x);
}
static inline double math_acos(double x) {
        return acos(x);
}
static inline double math_atan(double x) {
        return atan(x);
}
static inline double math_atan2(double y, double x) {
        return atan2(y, x);
}

/* Go: Sincos(x) -- compute both simultaneously */
static inline void math_sincos(double x, double* s, double* c) {
        *s = sin(x);
        *c = cos(x);
}

/* Degree <-> radian conversions */
static inline double math_deg_to_rad(double deg) {
        return deg * (MATH_PI / 180.0);
}
static inline double math_rad_to_deg(double rad) {
        return rad * (180.0 / MATH_PI);
}

/* ================================================================
 *  S5 -- Hyperbolic
 * ================================================================ */

static inline double math_sinh(double x) {
        return sinh(x);
}
static inline double math_cosh(double x) {
        return cosh(x);
}
static inline double math_tanh(double x) {
        return tanh(x);
}
static inline double math_asinh(double x) {
        return asinh(x);
}
static inline double math_acosh(double x) {
        return acosh(x);
}
static inline double math_atanh(double x) {
        return atanh(x);
}

/* ================================================================
 *  S6 -- Exponential & logarithmic
 * ================================================================ */

/* Go: Exp, Exp2, Expm1 */
static inline double math_exp(double x) {
        return exp(x);
}
static inline double math_exp2(double x) {
        return exp2(x);
}
static inline double math_expm1(double x) {
        return expm1(x);
}

/* Go: Log, Log1p, Log2, Log10, Logb */
static inline double math_log(double x) {
        return log(x);
}
static inline double math_log1p(double x) {
        return log1p(x);
}
static inline double math_log2(double x) {
        return log2(x);
}
static inline double math_log10(double x) {
        return log10(x);
}
static inline double math_logb(double x) {
        return logb(x);
}

/* Go: Ilogb(x) -- exponent as integer */
static inline int math_ilogb(double x) {
        return ilogb(x);
}

/* ================================================================
 *  S7 -- Power & root
 * ================================================================ */

/* Go: Sqrt, Cbrt, Pow, Pow10, Hypot */
static inline double math_sqrt(double x) {
        return sqrt(x);
}
static inline double math_cbrt(double x) {
        return cbrt(x);
}
static inline double math_pow(double x, double y) {
        return pow(x, y);
}
static inline double math_pow10(int n) {
        return pow(10.0, (double)n);
}
static inline double math_hypot(double p, double q) {
        return hypot(p, q);
}

/* Fast inverse square root (approximation -- useful for games/graphics) */
static inline float math_inv_sqrtf_fast(float x) {
        float xh = 0.5f * x;
        F32   r;
        r.f = x;
        r.u = 0x5F3759DFu - (r.u >> 1);
        r.f = r.f * (1.5f - xh * r.f * r.f); /* one Newton step */
        return r.f;
}

/* ================================================================
 *  S8 -- FP utilities
 * ================================================================ */

/* Go: Frexp(f) -- significand in [0.5, 1) and exponent */
static inline double math_frexp(double f, int* exp) {
        return frexp(f, exp);
}

/* Go: Ldexp(frac, exp) -- frac * 2^exp */
static inline double math_ldexp(double frac, int exp) {
        return ldexp(frac, exp);
}

/* Go: Nextafter(x, y) */
static inline double math_nextafter(double x, double y) {
        return nextafter(x, y);
}
static inline float math_nextafter32(float x, float y) {
        return nextafterf(x, y);
}

/* Go: FMA(x, y, z) -- fused multiply-add: x*y + z without rounding x*y */
static inline double math_fma(double x, double y, double z) {
        return fma(x, y, z);
}

/* ================================================================
 *  S9 -- Special functions
 * ================================================================ */

/* Go: Erf, Erfc */
static inline double math_erf(double x) {
        return erf(x);
}
static inline double math_erfc(double x) {
        return erfc(x);
}

/*
 * Go: Erfinv, Erfcinv
 * Rational approximation (Horner form).
 * Accuracy: ~6 ULP for |x| < 0.7, degrades near +/-1.
 */
double math_erfinv(double x);
double math_erfcinv(double x);

/* Go: Gamma(x) */
static inline double math_gamma(double x) {
        return tgamma(x);
}

/* Go: Lgamma(x) -- log|Gamma(x)| and sign */
static inline double math_lgamma(double x, int* sign) {
#        if defined(__USE_MISC) || defined(__APPLE__) || defined(__MACH__)
        return lgamma_r(x, sign);
#        else
        double v = lgamma(x);
        if (sign) *sign = (x > 0 || fmod(floor(-x), 2.0) != 0) ? 1 : -1;
        return v;
#        endif
}

/* Go: Bessel functions J0, J1, Jn, Y0, Y1, Yn */
#        if defined(__USE_XOPEN) || defined(__USE_MISC) || \
            defined(__APPLE__) || defined(__MACH__)
static inline double math_j0(double x) {
        return j0(x);
}
static inline double math_j1(double x) {
        return j1(x);
}
static inline double math_jn(int n, double x) {
        return jn(n, x);
}
static inline double math_y0(double x) {
        return y0(x);
}
static inline double math_y1(double x) {
        return y1(x);
}
static inline double math_yn(int n, double x) {
        return yn(n, x);
}
#        endif /* Bessel functions available */

/* ================================================================
 *  S10 -- Integer math
 * ================================================================ */

/* Absolute value for signed integers */
static inline int32_t math_abs32(int32_t x) {
        return x < 0 ? -x : x;
}
static inline int64_t math_abs64(int64_t x) {
        return x < 0 ? -x : x;
}

/* Greatest common divisor (Euclidean) */
uint64_t math_gcd(uint64_t a, uint64_t b);

/* Least common multiple -- returns 0 on overflow */
uint64_t math_lcm(uint64_t a, uint64_t b);

/* Floor of log2(n).  n must be > 0. */
uint32_t math_log2_floor(uint64_t n);

/* Ceiling of log2(n).  n must be > 0. */
uint32_t math_log2_ceil(uint64_t n);

/* Smallest power of two >= n.  Returns 1 for n == 0. */
uint64_t math_next_pow2(uint64_t n);

/* Integer square root (floor). */
uint64_t math_isqrt(uint64_t n);

/* Integer cube root (floor). */
uint64_t math_icbrt(uint64_t n);

/* Divide with ceiling */
static inline uint64_t math_div_ceil(uint64_t a, uint64_t b) {
        return (a + b - 1) / b;
}

/* Align up / down to power-of-two boundary (integer version) */
static inline uint64_t math_align_up(uint64_t n, uint64_t a) {
        return (n + a - 1) & ~(a - 1);
}
static inline uint64_t math_align_down(uint64_t n, uint64_t a) {
        return n & ~(a - 1);
}

/* ================================================================
 *  S11 -- Bit operations
 * ================================================================ */

/* Population count (number of set bits) */
uint32_t bit_popcount32(uint32_t x);
uint32_t bit_popcount64(uint64_t x);

/* Count leading zeros -- undefined for x == 0 */
uint32_t bit_clz32(uint32_t x);
uint32_t bit_clz64(uint64_t x);

/* Count trailing zeros -- undefined for x == 0 */
uint32_t bit_ctz32(uint32_t x);
uint32_t bit_ctz64(uint64_t x);

/* Byte-swap (endian flip) */
uint16_t bit_bswap16(uint16_t x);
uint32_t bit_bswap32(uint32_t x);
uint64_t bit_bswap64(uint64_t x);

/* Rotate left / right */
static inline uint32_t bit_rol32(uint32_t x, uint32_t n) {
        n &= 31;
        return (x << n) | (x >> (32 - n));
}
static inline uint32_t bit_ror32(uint32_t x, uint32_t n) {
        n &= 31;
        return (x >> n) | (x << (32 - n));
}
static inline uint64_t bit_rol64(uint64_t x, uint32_t n) {
        n &= 63;
        return (x << n) | (x >> (64 - n));
}
static inline uint64_t bit_ror64(uint64_t x, uint32_t n) {
        n &= 63;
        return (x >> n) | (x << (64 - n));
}

/* Extract bits [lo, lo+len) from x */
static inline uint64_t bit_extract(uint64_t x, uint32_t lo, uint32_t len) {
        return (x >> lo) & ((UINT64_C(1) << len) - 1);
}
/* Replace bits [lo, lo+len) in x with val */
static inline uint64_t bit_insert(uint64_t x,
                                  uint64_t val,
                                  uint32_t lo,
                                  uint32_t len) {
        uint64_t mask = ((UINT64_C(1) << len) - 1) << lo;
        return (x & ~mask) | ((val << lo) & mask);
}

/* Reverse all bits in a 32/64-bit value */
uint32_t bit_reverse32(uint32_t x);
uint64_t bit_reverse64(uint64_t x);

/* Next permutation of bits with same popcount (Gosper's hack) */
static inline uint64_t bit_next_perm(uint64_t v) {
        uint64_t t = v | (v - 1);
        return (t + 1) | (((~t & (~t - 1)) >> (bit_ctz64(v) + 1)));
}

/* ================================================================
 *  S12 -- Overflow-checked arithmetic
 *  Returns 1 on overflow (result is the wrapped/truncated value).
 * ================================================================ */

int math_add_u32(uint32_t a, uint32_t b, uint32_t* out);
int math_sub_u32(uint32_t a, uint32_t b, uint32_t* out);
int math_mul_u32(uint32_t a, uint32_t b, uint32_t* out);

int math_add_u64(uint64_t a, uint64_t b, uint64_t* out);
int math_sub_u64(uint64_t a, uint64_t b, uint64_t* out);
int math_mul_u64(uint64_t a, uint64_t b, uint64_t* out);

int math_add_i32(int32_t a, int32_t b, int32_t* out);
int math_sub_i32(int32_t a, int32_t b, int32_t* out);
int math_mul_i32(int32_t a, int32_t b, int32_t* out);

int math_add_i64(int64_t a, int64_t b, int64_t* out);
int math_sub_i64(int64_t a, int64_t b, int64_t* out);
int math_mul_i64(int64_t a, int64_t b, int64_t* out);

/* ================================================================
 *  S13 -- Fixed-point types
 *
 *  Q16_16: 16 integer bits, 16 fractional bits. Range +/-32767.9999...
 *  Q8_24:  8 integer bits, 24 fractional bits. Range +/-127.9999...
 *          Higher precision for values < 128, e.g. unit vectors.
 *
 *  The raw field is a plain int32_t in two's complement.
 *  Multiplication uses int64_t intermediary to avoid overflow.
 * ================================================================ */

typedef struct {
        int32_t raw;
} Q16_16; /* fixed-point 16.16 */
typedef struct {
        int32_t raw;
} Q8_24; /* fixed-point 8.24  */

#        define Q16_16_FRAC_BITS 16
#        define Q16_16_ONE       ((int32_t)1 << Q16_16_FRAC_BITS)
#        define Q8_24_FRAC_BITS  24
#        define Q8_24_ONE        ((int32_t)1 << Q8_24_FRAC_BITS)

/* Constructors */
static inline Q16_16 q16_from_int(int32_t i) {
        Q16_16 r;
        r.raw = i << Q16_16_FRAC_BITS;
        return r;
}
static inline Q16_16 q16_from_double(double f) {
        Q16_16 r;
        r.raw = (int32_t)(f * Q16_16_ONE);
        return r;
}
static inline double q16_to_double(Q16_16 q) {
        return (double)q.raw / Q16_16_ONE;
}
static inline int32_t q16_to_int(Q16_16 q) {
        return q.raw >> Q16_16_FRAC_BITS;
}

static inline Q8_24 q8_from_int(int32_t i) {
        Q8_24 r;
        r.raw = i << Q8_24_FRAC_BITS;
        return r;
}
static inline Q8_24 q8_from_double(double f) {
        Q8_24 r;
        r.raw = (int32_t)(f * Q8_24_ONE);
        return r;
}
static inline double q8_to_double(Q8_24 q) {
        return (double)q.raw / Q8_24_ONE;
}

/* Arithmetic -- Q16_16 */
static inline Q16_16 q16_add(Q16_16 a, Q16_16 b) {
        Q16_16 r;
        r.raw = a.raw + b.raw;
        return r;
}
static inline Q16_16 q16_sub(Q16_16 a, Q16_16 b) {
        Q16_16 r;
        r.raw = a.raw - b.raw;
        return r;
}
static inline Q16_16 q16_mul(Q16_16 a, Q16_16 b) {
        Q16_16 r;
        r.raw = (int32_t)(((int64_t)a.raw * b.raw) >> Q16_16_FRAC_BITS);
        return r;
}
static inline Q16_16 q16_div(Q16_16 a, Q16_16 b) {
        Q16_16 r;
        r.raw = (int32_t)(((int64_t)a.raw << Q16_16_FRAC_BITS) / b.raw);
        return r;
}
static inline Q16_16 q16_neg(Q16_16 a) {
        Q16_16 r;
        r.raw = -a.raw;
        return r;
}
static inline Q16_16 q16_abs(Q16_16 a) {
        Q16_16 r;
        r.raw = a.raw < 0 ? -a.raw : a.raw;
        return r;
}
static inline bool q16_lt(Q16_16 a, Q16_16 b) {
        return a.raw < b.raw;
}
static inline bool q16_eq(Q16_16 a, Q16_16 b) {
        return a.raw == b.raw;
}
static inline Q16_16 q16_min(Q16_16 a, Q16_16 b) {
        return a.raw < b.raw ? a : b;
}
static inline Q16_16 q16_max(Q16_16 a, Q16_16 b) {
        return a.raw > b.raw ? a : b;
}

/* Arithmetic -- Q8_24 */
static inline Q8_24 q8_add(Q8_24 a, Q8_24 b) {
        Q8_24 r;
        r.raw = a.raw + b.raw;
        return r;
}
static inline Q8_24 q8_sub(Q8_24 a, Q8_24 b) {
        Q8_24 r;
        r.raw = a.raw - b.raw;
        return r;
}
static inline Q8_24 q8_mul(Q8_24 a, Q8_24 b) {
        Q8_24 r;
        r.raw = (int32_t)(((int64_t)a.raw * b.raw) >> Q8_24_FRAC_BITS);
        return r;
}
static inline Q8_24 q8_div(Q8_24 a, Q8_24 b) {
        Q8_24 r;
        r.raw = (int32_t)(((int64_t)a.raw << Q8_24_FRAC_BITS) / b.raw);
        return r;
}

/* ================================================================
 *  S14 -- 2D / 3D / 4D vector types
 *
 *  Plain structs with named fields -- valid ISO C99 everywhere.
 *  No unions, no anonymous structs, no compiler extensions.
 *
 *  Field access:  v.x, v.y, v.z, v.w
 *  Array access:  vec2_get(v, i) / vec3_get(v, i) etc.
 *  Colour aliases via explicit constructors: vec4_rgba(r,g,b,a)
 *  Conversions:   vec3_from_vec4(v), vec4_from_vec3(v, w), ...
 *
 *  Integer variants: Vec2i, Vec3i (int32_t fields x/y/z)
 *  Double  variants: Vec2d, Vec3d, Vec4d (double fields x/y/z/w)
 * ================================================================ */

typedef struct {
        float x, y;
} Vec2;
typedef struct {
        float x, y, z;
} Vec3;
typedef struct {
        float x, y, z, w;
} Vec4;
typedef struct {
        int32_t x, y;
} Vec2i;
typedef struct {
        int32_t x, y, z;
} Vec3i;
typedef struct {
        double x, y;
} Vec2d;
typedef struct {
        double x, y, z;
} Vec3d;
typedef struct {
        double x, y, z, w;
} Vec4d;

/* Array access -- implementation-defined but works on all mainstream
   platforms (x86, ARM, RISC-V) where structs are packed without padding
   for uniform float/int/double members. */
static inline float vec2_get(Vec2 v, int i) {
        return ((float*)&v)[i];
}
static inline float vec3_get(Vec3 v, int i) {
        return ((float*)&v)[i];
}
static inline float vec4_get(Vec4 v, int i) {
        return ((float*)&v)[i];
}
static inline int32_t vec2i_get(Vec2i v, int i) {
        return ((int32_t*)&v)[i];
}
static inline int32_t vec3i_get(Vec3i v, int i) {
        return ((int32_t*)&v)[i];
}
static inline double vec2d_get(Vec2d v, int i) {
        return ((double*)&v)[i];
}
static inline double vec3d_get(Vec3d v, int i) {
        return ((double*)&v)[i];
}
static inline double vec4d_get(Vec4d v, int i) {
        return ((double*)&v)[i];
}

/* Conversions between types */
static inline Vec2 vec2_from_vec3(Vec3 v) {
        Vec2 r;
        r.x = v.x;
        r.y = v.y;
        return r;
}
static inline Vec3 vec3_from_vec4(Vec4 v) {
        Vec3 r;
        r.x = v.x;
        r.y = v.y;
        r.z = v.z;
        return r;
}
static inline Vec4 vec4_from_vec3(Vec3 v, float w) {
        Vec4 r;
        r.x = v.x;
        r.y = v.y;
        r.z = v.z;
        r.w = w;
        return r;
}

/* Colour alias constructors (map r/g/b/a -> x/y/z/w) */
static inline Vec4 vec4_rgba(float r, float g, float b, float a) {
        Vec4 v;
        v.x = r;
        v.y = g;
        v.z = b;
        v.w = a;
        return v;
}
static inline Vec3 vec3_rgb(float r, float g, float b) {
        Vec3 v;
        v.x = r;
        v.y = g;
        v.z = b;
        return v;
}

/* UV alias constructor */
static inline Vec2 vec2_uv(float u, float v_) {
        Vec2 v;
        v.x = u;
        v.y = v_;
        return v;
}

/* ---- Vec2 --------------------------------------------------- */
static inline Vec2 vec2(float x, float y) {
        Vec2 v;
        v.x = x;
        v.y = y;
        return v;
}
static inline Vec2 vec2_zero(void) {
        return vec2(0, 0);
}
static inline Vec2 vec2_one(void) {
        return vec2(1, 1);
}
static inline Vec2 vec2_add(Vec2 a, Vec2 b) {
        return vec2(a.x + b.x, a.y + b.y);
}
static inline Vec2 vec2_sub(Vec2 a, Vec2 b) {
        return vec2(a.x - b.x, a.y - b.y);
}
static inline Vec2 vec2_mul(Vec2 a, Vec2 b) {
        return vec2(a.x * b.x, a.y * b.y);
}
static inline Vec2 vec2_scale(Vec2 a, float s) {
        return vec2(a.x * s, a.y * s);
}
static inline Vec2 vec2_neg(Vec2 a) {
        return vec2(-a.x, -a.y);
}
static inline float vec2_dot(Vec2 a, Vec2 b) {
        return a.x * b.x + a.y * b.y;
}
static inline float vec2_len2(Vec2 a) {
        return vec2_dot(a, a);
}
static inline float vec2_len(Vec2 a) {
        return sqrtf(vec2_len2(a));
}
static inline Vec2 vec2_norm(Vec2 a) {
        float l = vec2_len(a);
        return l > 0 ? vec2_scale(a, 1.0f / l) : vec2_zero();
}
static inline float vec2_cross(Vec2 a, Vec2 b) {
        return a.x * b.y - a.y * b.x;
}
static inline Vec2 vec2_lerp(Vec2 a, Vec2 b, float t) {
        return vec2_add(a, vec2_scale(vec2_sub(b, a), t));
}
static inline float vec2_dist(Vec2 a, Vec2 b) {
        return vec2_len(vec2_sub(b, a));
}
static inline Vec2 vec2_perp(Vec2 a) {
        return vec2(-a.y, a.x);
}
static inline Vec2 vec2_reflect(Vec2 v, Vec2 n) {
        return vec2_sub(v, vec2_scale(n, 2.0f * vec2_dot(v, n)));
}

/* ---- Vec3 --------------------------------------------------- */
static inline Vec3 vec3(float x, float y, float z) {
        Vec3 v;
        v.x = x;
        v.y = y;
        v.z = z;
        return v;
}
static inline Vec3 vec3_zero(void) {
        return vec3(0, 0, 0);
}
static inline Vec3 vec3_one(void) {
        return vec3(1, 1, 1);
}
static inline Vec3 vec3_add(Vec3 a, Vec3 b) {
        return vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}
static inline Vec3 vec3_sub(Vec3 a, Vec3 b) {
        return vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}
static inline Vec3 vec3_mul(Vec3 a, Vec3 b) {
        return vec3(a.x * b.x, a.y * b.y, a.z * b.z);
}
static inline Vec3 vec3_scale(Vec3 a, float s) {
        return vec3(a.x * s, a.y * s, a.z * s);
}
static inline Vec3 vec3_neg(Vec3 a) {
        return vec3(-a.x, -a.y, -a.z);
}
static inline float vec3_dot(Vec3 a, Vec3 b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
}
static inline float vec3_len2(Vec3 a) {
        return vec3_dot(a, a);
}
static inline float vec3_len(Vec3 a) {
        return sqrtf(vec3_len2(a));
}
static inline Vec3 vec3_norm(Vec3 a) {
        float l = vec3_len(a);
        return l > 0 ? vec3_scale(a, 1.0f / l) : vec3_zero();
}
static inline Vec3 vec3_cross(Vec3 a, Vec3 b) {
        return vec3(a.y * b.z - a.z * b.y,
                    a.z * b.x - a.x * b.z,
                    a.x * b.y - a.y * b.x);
}
static inline Vec3 vec3_lerp(Vec3 a, Vec3 b, float t) {
        return vec3_add(a, vec3_scale(vec3_sub(b, a), t));
}
static inline float vec3_dist(Vec3 a, Vec3 b) {
        return vec3_len(vec3_sub(b, a));
}
static inline Vec3 vec3_reflect(Vec3 v, Vec3 n) {
        return vec3_sub(v, vec3_scale(n, 2.0f * vec3_dot(v, n)));
}
static inline Vec3 vec3_refract(Vec3 v, Vec3 n, float eta) {
        float d = vec3_dot(v, n);
        float k = 1.0f - eta * eta * (1.0f - d * d);
        return k < 0 ? vec3_zero()
                     : vec3_sub(vec3_scale(v, eta),
                                vec3_scale(n, eta * d + sqrtf(k)));
}
/* Extract sub-vector */
static inline Vec2 vec3_xy(Vec3 v) {
        return vec2(v.x, v.y);
}

/* ---- Vec4 --------------------------------------------------- */
static inline Vec4 vec4(float x, float y, float z, float w) {
        Vec4 v;
        v.x = x;
        v.y = y;
        v.z = z;
        v.w = w;
        return v;
}
static inline Vec4 vec4_zero(void) {
        return vec4(0, 0, 0, 0);
}
static inline Vec4 vec4_one(void) {
        return vec4(1, 1, 1, 1);
}
static inline Vec4 vec4_add(Vec4 a, Vec4 b) {
        return vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
}
static inline Vec4 vec4_sub(Vec4 a, Vec4 b) {
        return vec4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
}
static inline Vec4 vec4_scale(Vec4 a, float s) {
        return vec4(a.x * s, a.y * s, a.z * s, a.w * s);
}
static inline float vec4_dot(Vec4 a, Vec4 b) {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}
static inline float vec4_len(Vec4 a) {
        return sqrtf(vec4_dot(a, a));
}
static inline Vec4 vec4_norm(Vec4 a) {
        float l = vec4_len(a);
        return l > 0 ? vec4_scale(a, 1.0f / l) : vec4_zero();
}
static inline Vec4 vec4_lerp(Vec4 a, Vec4 b, float t) {
        return vec4_add(a, vec4_scale(vec4_sub(b, a), t));
}
/* Extract sub-vectors */
static inline Vec3 vec4_xyz(Vec4 v) {
        return vec3(v.x, v.y, v.z);
}
static inline Vec2 vec4_lo(Vec4 v) {
        return vec2(v.x, v.y);
}
static inline Vec2 vec4_hi(Vec4 v) {
        return vec2(v.z, v.w);
}

/* ---- Quaternion (stored as Vec4: xyzw, w = real part) ------- */
static inline Vec4 quat_identity(void) {
        return vec4(0, 0, 0, 1);
}
static inline Vec4 quat_mul(Vec4 a, Vec4 b) {
        return vec4(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                    a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                    a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                    a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}
static inline Vec4 quat_conj(Vec4 q) {
        return vec4(-q.x, -q.y, -q.z, q.w);
}
static inline Vec4 quat_norm(Vec4 q) {
        return vec4_norm(q);
}
static inline Vec4 quat_from_axis_angle(Vec3 axis, float angle) {
        float s = sinf(angle * 0.5f), c = cosf(angle * 0.5f);
        Vec3  a = vec3_norm(axis);
        return vec4(a.x * s, a.y * s, a.z * s, c);
}
static inline Vec3 quat_rotate_vec3(Vec4 q, Vec3 v) {
        Vec3 qv = vec3(q.x, q.y, q.z);
        Vec3 t  = vec3_scale(vec3_cross(qv, v), 2.0f);
        return vec3_add(vec3_add(v, vec3_scale(t, q.w)), vec3_cross(qv, t));
}
static inline Vec4 quat_slerp(Vec4 a, Vec4 b, float t) {
        float d = vec4_dot(a, b);
        if (d < 0) {
                b = vec4_scale(b, -1);
                d = -d;
        }
        if (d > 0.9995f) return vec4_norm(vec4_lerp(a, b, t));
        float th0 = acosf(d), th = th0 * t;
        float s0 = cosf(th) - d * sinf(th) / sinf(th0);
        float s1 = sinf(th) / sinf(th0);
        return vec4_add(vec4_scale(a, s0), vec4_scale(b, s1));
}

/* ---- 4x4 column-major matrix -------------------------------- */
typedef struct {
        float e[16];
} Mat4;

static inline Mat4 mat4_identity(void) {
        Mat4 m = {0};
        m.e[0] = m.e[5] = m.e[10] = m.e[15] = 1;
        return m;
}
static inline Mat4 mat4_mul(Mat4 a, Mat4 b) {
        Mat4 r = {0};
        int  i, j, k;
        for (i = 0; i < 4; i++)
                for (j = 0; j < 4; j++)
                        for (k = 0; k < 4; k++)
                                r.e[i * 4 + j] +=
                                    a.e[k * 4 + j] * b.e[i * 4 + k];
        return r;
}
static inline Vec4 mat4_mul_vec4(Mat4 m, Vec4 v) {
        return vec4(
            m.e[0] * v.x + m.e[4] * v.y + m.e[8] * v.z + m.e[12] * v.w,
            m.e[1] * v.x + m.e[5] * v.y + m.e[9] * v.z + m.e[13] * v.w,
            m.e[2] * v.x + m.e[6] * v.y + m.e[10] * v.z + m.e[14] * v.w,
            m.e[3] * v.x + m.e[7] * v.y + m.e[11] * v.z + m.e[15] * v.w);
}
Mat4 mat4_perspective(float fovy_rad, float aspect, float near, float far);
Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up);
Mat4 mat4_translate(Vec3 t);
Mat4 mat4_scale_v(Vec3 s);
Mat4 mat4_rotate(Vec3 axis, float angle);
Mat4 mat4_transpose(Mat4 m);
Mat4 mat4_inverse(Mat4 m); /* returns identity if not invertible */

/* ================================================================
 *  S15 -- Hashing
 * ================================================================ */

/*
 * SipHash-1-3 -- cryptographically strong PRF.
 * Use for hash tables that accept untrusted keys.
 * key must be exactly 16 bytes.
 */
uint64_t siphash13(const void* data, size_t len, const uint8_t key[16]);

/* xxHash32 / xxHash64 -- fast non-cryptographic hashes. seed=0 is fine. */
uint32_t xxhash32(const void* data, size_t len, uint32_t seed);
uint64_t xxhash64(const void* data, size_t len, uint64_t seed);

/* Murmur3-32 -- good distribution, widely used. */
uint32_t murmur3_32(const void* data, size_t len, uint32_t seed);

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#        ifdef BAREMATH_IMPLEMENTATION

#                include <assert.h>
#                include <float.h>
#                include <limits.h>
#                include <string.h>

/* ---- erfinv / erfcinv --------------------------------------- */

double math_erfinv(double x) {
        /* Rational approximation by J.M. Blair et al. */
        double w = -log((1.0 - x) * (1.0 + x));
        double p;
        if (w < 5.0) {
                w -= 2.5;
                p = 2.81022636e-08;
                p = 3.43273939e-07 + p * w;
                p = -3.5233877e-06 + p * w;
                p = -4.39150654e-06 + p * w;
                p = 0.00021858087 + p * w;
                p = -0.00125372503 + p * w;
                p = -0.00417768164 + p * w;
                p = 0.246640727 + p * w;
                p = 1.50140941 + p * w;
        } else {
                w = sqrt(w) - 3.0;
                p = -0.000200214257;
                p = 0.000100950558 + p * w;
                p = 0.00134934322 + p * w;
                p = -0.00367342844 + p * w;
                p = 0.00573950773 + p * w;
                p = -0.0076224613 + p * w;
                p = 0.00943887047 + p * w;
                p = 1.00167406 + p * w;
                p = 2.83297682 + p * w;
        }
        return p * x;
}

double math_erfcinv(double x) {
        return math_erfinv(1.0 - x);
}

/* ---- Integer math ------------------------------------------ */

uint64_t math_gcd(uint64_t a, uint64_t b) {
        while (b) {
                uint64_t t = b;
                b          = a % b;
                a          = t;
        }
        return a;
}

uint64_t math_lcm(uint64_t a, uint64_t b) {
        if (!a || !b) return 0;
        uint64_t g = math_gcd(a, b);
        uint64_t r;
        if (math_mul_u64(a / g, b, &r)) return 0; /* overflow */
        return r;
}

uint32_t math_log2_floor(uint64_t n) {
        assert(n > 0 && "math_log2_floor: n must be > 0");
        return 63 - bit_clz64(n);
}

uint32_t math_log2_ceil(uint64_t n) {
        assert(n > 0 && "math_log2_ceil: n must be > 0");
        return (n == 1) ? 0 : (64 - bit_clz64(n - 1));
}

uint64_t math_next_pow2(uint64_t n) {
        if (n <= 1) return 1;
        return UINT64_C(1) << math_log2_ceil(n);
}

uint64_t math_isqrt(uint64_t n) {
        if (n == 0) return 0;
        uint64_t x = n, y = (x + 1) / 2;
        while (y < x) {
                x = y;
                y = (x + n / x) / 2;
        }
        return x;
}

uint64_t math_icbrt(uint64_t n) {
        if (n == 0) return 0;
        uint64_t x = (uint64_t)cbrt((double)n);
        /* adjust for floating-point rounding */
        while (x * x * x > n) x--;
        while ((x + 1) * (x + 1) * (x + 1) <= n) x++;
        return x;
}

/* ---- Bit operations ---------------------------------------- */

#                if defined(__GNUC__) || defined(__clang__)
uint32_t bit_popcount32(uint32_t x) {
        return (uint32_t)__builtin_popcount(x);
}
uint32_t bit_popcount64(uint64_t x) {
        return (uint32_t)__builtin_popcountll(x);
}
uint32_t bit_clz32(uint32_t x) {
        return (uint32_t)__builtin_clz(x);
}
uint32_t bit_clz64(uint64_t x) {
        return (uint32_t)__builtin_clzll(x);
}
uint32_t bit_ctz32(uint32_t x) {
        return (uint32_t)__builtin_ctz(x);
}
uint32_t bit_ctz64(uint64_t x) {
        return (uint32_t)__builtin_ctzll(x);
}
#                elif defined(_MSC_VER)
#                        include <intrin.h>
uint32_t bit_popcount32(uint32_t x) {
        return __popcnt(x);
}
uint32_t bit_popcount64(uint64_t x) {
        return (uint32_t)__popcnt64(x);
}
uint32_t bit_clz32(uint32_t x) {
        unsigned long r;
        _BitScanReverse(&r, x);
        return 31 - r;
}
uint32_t bit_clz64(uint64_t x) {
        unsigned long r;
        _BitScanReverse64(&r, x);
        return 63 - r;
}
uint32_t bit_ctz32(uint32_t x) {
        unsigned long r;
        _BitScanForward(&r, x);
        return r;
}
uint32_t bit_ctz64(uint64_t x) {
        unsigned long r;
        _BitScanForward64(&r, x);
        return r;
}
#                else /* portable fallbacks */
uint32_t bit_popcount32(uint32_t x) {
        x = x - ((x >> 1) & 0x55555555u);
        x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
        return ((x + (x >> 4)) & 0x0F0F0F0Fu) * 0x01010101u >> 24;
}
uint32_t bit_popcount64(uint64_t x) {
        return bit_popcount32((uint32_t)x) +
               bit_popcount32((uint32_t)(x >> 32));
}
uint32_t bit_clz32(uint32_t x) {
        uint32_t n = 0;
        if (!(x >> 16)) {
                n += 16;
                x <<= 16;
        }
        if (!(x >> 24)) {
                n += 8;
                x <<= 8;
        }
        if (!(x >> 28)) {
                n += 4;
                x <<= 4;
        }
        if (!(x >> 30)) {
                n += 2;
                x <<= 2;
        }
        if (!(x >> 31)) n++;
        return n;
}
uint32_t bit_clz64(uint64_t x) {
        uint32_t hi = (uint32_t)(x >> 32);
        return hi ? bit_clz32(hi) : 32 + bit_clz32((uint32_t)x);
}
uint32_t bit_ctz32(uint32_t x) {
        return bit_popcount32((x & (0 - x)) - 1);
}
uint32_t bit_ctz64(uint64_t x) {
        return bit_popcount64((x & (0 - x)) - 1);
}
#                endif

uint16_t bit_bswap16(uint16_t x) {
        return (uint16_t)((x >> 8) | (x << 8));
}
uint32_t bit_bswap32(uint32_t x) {
        return ((x & 0xFF000000u) >> 24) | ((x & 0x00FF0000u) >> 8) |
               ((x & 0x0000FF00u) << 8) | ((x & 0x000000FFu) << 24);
}
uint64_t bit_bswap64(uint64_t x) {
        return ((uint64_t)bit_bswap32((uint32_t)x) << 32) |
               (uint64_t)bit_bswap32((uint32_t)(x >> 32));
}

uint32_t bit_reverse32(uint32_t x) {
        x = ((x & 0xAAAAAAAAu) >> 1) | ((x & 0x55555555u) << 1);
        x = ((x & 0xCCCCCCCCu) >> 2) | ((x & 0x33333333u) << 2);
        x = ((x & 0xF0F0F0F0u) >> 4) | ((x & 0x0F0F0F0Fu) << 4);
        return bit_bswap32(x);
}
uint64_t bit_reverse64(uint64_t x) {
        return ((uint64_t)bit_reverse32((uint32_t)x) << 32) |
               (uint64_t)bit_reverse32((uint32_t)(x >> 32));
}

/* ---- Overflow arithmetic ----------------------------------- */

int math_add_u32(uint32_t a, uint32_t b, uint32_t* out) {
        *out = a + b;
        return *out < a;
}
int math_sub_u32(uint32_t a, uint32_t b, uint32_t* out) {
        *out = a - b;
        return b > a;
}
int math_mul_u32(uint32_t a, uint32_t b, uint32_t* out) {
        uint64_t r = (uint64_t)a * b;
        *out       = (uint32_t)r;
        return r > UINT32_MAX;
}
int math_add_u64(uint64_t a, uint64_t b, uint64_t* out) {
        *out = a + b;
        return *out < a;
}
int math_sub_u64(uint64_t a, uint64_t b, uint64_t* out) {
        *out = a - b;
        return b > a;
}
int math_mul_u64(uint64_t a, uint64_t b, uint64_t* out) {
        if (a && b > UINT64_MAX / a) {
                *out = a * b;
                return 1;
        }
        *out = a * b;
        return 0;
}
int math_add_i32(int32_t a, int32_t b, int32_t* out) {
        *out = (int32_t)((uint32_t)a + (uint32_t)b);
        return (b > 0 && a > INT32_MAX - b) || (b < 0 && a < INT32_MIN - b);
}
int math_sub_i32(int32_t a, int32_t b, int32_t* out) {
        *out = (int32_t)((uint32_t)a - (uint32_t)b);
        return (b < 0 && a > INT32_MAX + b) || (b > 0 && a < INT32_MIN + b);
}
int math_mul_i32(int32_t a, int32_t b, int32_t* out) {
        int64_t r = (int64_t)a * b;
        *out      = (int32_t)r;
        return r > INT32_MAX || r < INT32_MIN;
}
int math_add_i64(int64_t a, int64_t b, int64_t* out) {
        *out = (int64_t)((uint64_t)a + (uint64_t)b);
        return (b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b);
}
int math_sub_i64(int64_t a, int64_t b, int64_t* out) {
        *out = (int64_t)((uint64_t)a - (uint64_t)b);
        return (b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b);
}
int math_mul_i64(int64_t a, int64_t b, int64_t* out) {
        /* Use __int128 if available */
#                if defined(__GNUC__) || defined(__clang__)
        __int128 r = (__int128)a * b;
        *out       = (int64_t)r;
        return r > INT64_MAX || r < ((__int128)INT64_MIN);
#                else
        *out = a * b;
        return (a != 0 && *out / a != b);
#                endif
}

/* ---- Mat4 -------------------------------------------------- */

Mat4 mat4_transpose(Mat4 m) {
        Mat4 r;
        int  i, j;
        for (i = 0; i < 4; i++)
                for (j = 0; j < 4; j++) r.e[i * 4 + j] = m.e[j * 4 + i];
        return r;
}

Mat4 mat4_translate(Vec3 t) {
        Mat4 m  = mat4_identity();
        m.e[12] = t.x;
        m.e[13] = t.y;
        m.e[14] = t.z;
        return m;
}

Mat4 mat4_scale_v(Vec3 s) {
        Mat4 m  = mat4_identity();
        m.e[0]  = s.x;
        m.e[5]  = s.y;
        m.e[10] = s.z;
        return m;
}

Mat4 mat4_rotate(Vec3 axis, float angle) {
        Vec3  a = vec3_norm(axis);
        float s = sinf(angle), c = cosf(angle), t = 1.0f - c;
        Mat4  m = mat4_identity();
        m.e[0]  = t * a.x * a.x + c;
        m.e[1]  = t * a.x * a.y + s * a.z;
        m.e[2]  = t * a.x * a.z - s * a.y;
        m.e[4]  = t * a.x * a.y - s * a.z;
        m.e[5]  = t * a.y * a.y + c;
        m.e[6]  = t * a.y * a.z + s * a.x;
        m.e[8]  = t * a.x * a.z + s * a.y;
        m.e[9]  = t * a.y * a.z - s * a.x;
        m.e[10] = t * a.z * a.z + c;
        return m;
}

Mat4 mat4_perspective(float fovy_rad, float aspect, float near, float far) {
        float f = 1.0f / tanf(fovy_rad * 0.5f);
        Mat4  m = {0};
        m.e[0]  = f / aspect;
        m.e[5]  = f;
        m.e[10] = (far + near) / (near - far);
        m.e[11] = -1.0f;
        m.e[14] = (2.0f * far * near) / (near - far);
        return m;
}

Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up) {
        Vec3 f  = vec3_norm(vec3_sub(center, eye));
        Vec3 s  = vec3_norm(vec3_cross(f, up));
        Vec3 u  = vec3_cross(s, f);
        Mat4 m  = mat4_identity();
        m.e[0]  = s.x;
        m.e[4]  = s.y;
        m.e[8]  = s.z;
        m.e[1]  = u.x;
        m.e[5]  = u.y;
        m.e[9]  = u.z;
        m.e[2]  = -f.x;
        m.e[6]  = -f.y;
        m.e[10] = -f.z;
        m.e[12] = -vec3_dot(s, eye);
        m.e[13] = -vec3_dot(u, eye);
        m.e[14] = vec3_dot(f, eye);
        return m;
}

Mat4 mat4_inverse(Mat4 m) {
        /* Cofactor expansion -- standard 4x4 inverse */
        float* a = m.e;
        float  inv[16], det;
        int    i;
        inv[0]  = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] -
                  a[9] * a[6] * a[15] + a[9] * a[7] * a[14] +
                  a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
        inv[4]  = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] +
                  a[8] * a[6] * a[15] - a[8] * a[7] * a[14] -
                  a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
        inv[8]  = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] -
                  a[8] * a[5] * a[15] + a[8] * a[7] * a[13] +
                  a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
        inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] +
                  a[8] * a[5] * a[14] - a[8] * a[6] * a[13] -
                  a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
        inv[1]  = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] +
                  a[9] * a[2] * a[15] - a[9] * a[3] * a[14] -
                  a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
        inv[5]  = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] -
                  a[8] * a[2] * a[15] + a[8] * a[3] * a[14] +
                  a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
        inv[9]  = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] +
                  a[8] * a[1] * a[15] - a[8] * a[3] * a[13] -
                  a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
        inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] -
                  a[8] * a[1] * a[14] + a[8] * a[2] * a[13] +
                  a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
        inv[2]  = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] -
                  a[5] * a[2] * a[15] + a[5] * a[3] * a[14] +
                  a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
        inv[6]  = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] +
                  a[4] * a[2] * a[15] - a[4] * a[3] * a[14] -
                  a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
        inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] -
                  a[4] * a[1] * a[15] + a[4] * a[3] * a[13] +
                  a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
        inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] +
                  a[4] * a[1] * a[14] - a[4] * a[2] * a[13] -
                  a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
        inv[3]  = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] +
                  a[5] * a[2] * a[11] - a[5] * a[3] * a[10] -
                  a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
        inv[7]  = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] -
                  a[4] * a[2] * a[11] + a[4] * a[3] * a[10] +
                  a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
        inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] +
                  a[4] * a[1] * a[11] - a[4] * a[3] * a[9] -
                  a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
        inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] -
                  a[4] * a[1] * a[10] + a[4] * a[2] * a[9] +
                  a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
        det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
        if (det == 0.0f) return mat4_identity();
        det = 1.0f / det;
        Mat4 r;
        for (i = 0; i < 16; i++) r.e[i] = inv[i] * det;
        return r;
}

/* ---- SipHash-1-3 ------------------------------------------- */

#                define _SIP_ROTL(x, n) (((x) << (n)) | ((x) >> (64 - (n))))
#                define _SIP_ROUND(v0, v1, v2, v3) \
                        v0 += v1;                  \
                        v1 = _SIP_ROTL(v1, 13);    \
                        v1 ^= v0;                  \
                        v0 = _SIP_ROTL(v0, 32);    \
                        v2 += v3;                  \
                        v3 = _SIP_ROTL(v3, 16);    \
                        v3 ^= v2;                  \
                        v0 += v3;                  \
                        v3 = _SIP_ROTL(v3, 21);    \
                        v3 ^= v0;                  \
                        v2 += v1;                  \
                        v1 = _SIP_ROTL(v1, 17);    \
                        v1 ^= v2;                  \
                        v2 = _SIP_ROTL(v2, 32)

static uint64_t _sip_u64le(const uint8_t* p) {
        return (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) |
               ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) |
               ((uint64_t)p[5] << 40) | ((uint64_t)p[6] << 48) |
               ((uint64_t)p[7] << 56);
}

uint64_t siphash13(const void* data, size_t len, const uint8_t key[16]) {
        uint64_t       k0 = _sip_u64le(key), k1 = _sip_u64le(key + 8);
        uint64_t       v0 = k0 ^ UINT64_C(0x736f6d6570736575);
        uint64_t       v1 = k1 ^ UINT64_C(0x646f72616e646f6d);
        uint64_t       v2 = k0 ^ UINT64_C(0x6c7967656e657261);
        uint64_t       v3 = k1 ^ UINT64_C(0x7465646279746573);
        const uint8_t *p = (const uint8_t*)data, *end = p + (len & ~(size_t)7);
        for (; p != end; p += 8) {
                uint64_t m = _sip_u64le(p);
                v3 ^= m;
                _SIP_ROUND(v0, v1, v2, v3);
                v0 ^= m;
        }
        uint64_t m = (uint64_t)(len & 0xFF) << 56;
        switch (len & 7) {
                case 7:
                        m |= (uint64_t)p[6] << 48; /* fall through */
                case 6:
                        m |= (uint64_t)p[5] << 40; /* fall through */
                case 5:
                        m |= (uint64_t)p[4] << 32; /* fall through */
                case 4:
                        m |= (uint64_t)p[3] << 24; /* fall through */
                case 3:
                        m |= (uint64_t)p[2] << 16; /* fall through */
                case 2:
                        m |= (uint64_t)p[1] << 8; /* fall through */
                case 1:
                        m |= (uint64_t)p[0];
                default:
                        break;
        }
        v3 ^= m;
        _SIP_ROUND(v0, v1, v2, v3);
        v0 ^= m;
        v2 ^= 0xFF;
        _SIP_ROUND(v0, v1, v2, v3);
        _SIP_ROUND(v0, v1, v2, v3);
        _SIP_ROUND(v0, v1, v2, v3);
        return v0 ^ v1 ^ v2 ^ v3;
}

/* ---- xxHash32 ---------------------------------------------- */

#                define _XX32_P1 UINT32_C(2654435761)
#                define _XX32_P2 UINT32_C(2246822519)
#                define _XX32_P3 UINT32_C(3266489917)
#                define _XX32_P4 UINT32_C(668265263)
#                define _XX32_P5 UINT32_C(374761393)

static uint32_t _xx32_r32(const void* p) {
        uint32_t v;
        memcpy(&v, p, 4);
        return v;
}
static uint32_t _xx32_rnd(uint32_t a, uint32_t i) {
        return bit_rol32(a + i * _XX32_P2, 13) * _XX32_P1;
}

uint32_t xxhash32(const void* data, size_t len, uint32_t seed) {
        const uint8_t *p = (const uint8_t*)data, *end = p + len;
        uint32_t       h;
        if (len >= 16) {
                const uint8_t* lim = end - 16;
                uint32_t v1 = seed + _XX32_P1 + _XX32_P2, v2 = seed + _XX32_P2,
                         v3 = seed, v4 = seed - _XX32_P1;
                do {
                        v1 = _xx32_rnd(v1, _xx32_r32(p));
                        p += 4;
                        v2 = _xx32_rnd(v2, _xx32_r32(p));
                        p += 4;
                        v3 = _xx32_rnd(v3, _xx32_r32(p));
                        p += 4;
                        v4 = _xx32_rnd(v4, _xx32_r32(p));
                        p += 4;
                } while (p <= lim);
                h = bit_rol32(v1, 1) + bit_rol32(v2, 7) + bit_rol32(v3, 12) +
                    bit_rol32(v4, 18);
        } else {
                h = seed + _XX32_P5;
        }
        h += (uint32_t)len;
        while (p + 4 <= end) {
                h = bit_rol32(h + _xx32_r32(p) * _XX32_P3, 17) * _XX32_P4;
                p += 4;
        }
        while (p < end) {
                h = bit_rol32(h + (uint32_t)*p * _XX32_P5, 11) * _XX32_P1;
                p++;
        }
        h ^= h >> 15;
        h *= _XX32_P2;
        h ^= h >> 13;
        h *= _XX32_P3;
        h ^= h >> 16;
        return h;
}

/* ---- xxHash64 ---------------------------------------------- */

#                define _XX64_P1 UINT64_C(11400714785074694791)
#                define _XX64_P2 UINT64_C(14029467366897019727)
#                define _XX64_P3 UINT64_C(1609587929392839161)
#                define _XX64_P4 UINT64_C(9650029242287828579)
#                define _XX64_P5 UINT64_C(2870177450012600261)

static uint64_t _xx64_r64(const void* p) {
        uint64_t v;
        memcpy(&v, p, 8);
        return v;
}
static uint32_t _xx64_r32(const void* p) {
        uint32_t v;
        memcpy(&v, p, 4);
        return v;
}
static uint64_t _xx64_rnd(uint64_t a, uint64_t i) {
        return bit_rol64(a + i * _XX64_P2, 31) * _XX64_P1;
}
static uint64_t _xx64_mrg(uint64_t a, uint64_t v) {
        return (a ^ _xx64_rnd(0, v)) * _XX64_P1 + _XX64_P4;
}

uint64_t xxhash64(const void* data, size_t len, uint64_t seed) {
        const uint8_t *p = (const uint8_t*)data, *end = p + len;
        uint64_t       h;
        if (len >= 32) {
                const uint8_t* lim = end - 32;
                uint64_t v1 = seed + _XX64_P1 + _XX64_P2, v2 = seed + _XX64_P2,
                         v3 = seed, v4 = seed - _XX64_P1;
                do {
                        v1 = _xx64_rnd(v1, _xx64_r64(p));
                        p += 8;
                        v2 = _xx64_rnd(v2, _xx64_r64(p));
                        p += 8;
                        v3 = _xx64_rnd(v3, _xx64_r64(p));
                        p += 8;
                        v4 = _xx64_rnd(v4, _xx64_r64(p));
                        p += 8;
                } while (p <= lim);
                h = bit_rol64(v1, 1) + bit_rol64(v2, 7) + bit_rol64(v3, 12) +
                    bit_rol64(v4, 18);
                h = _xx64_mrg(h, v1);
                h = _xx64_mrg(h, v2);
                h = _xx64_mrg(h, v3);
                h = _xx64_mrg(h, v4);
        } else {
                h = seed + _XX64_P5;
        }
        h += (uint64_t)len;
        while (p + 8 <= end) {
                h = bit_rol64(h ^ _xx64_rnd(0, _xx64_r64(p)), 27) * _XX64_P1 +
                    _XX64_P4;
                p += 8;
        }
        if (p + 4 <= end) {
                h = bit_rol64(h ^ ((uint64_t)_xx64_r32(p) * _XX64_P1), 23) *
                        _XX64_P2 +
                    _XX64_P3;
                p += 4;
        }
        while (p < end) {
                h = bit_rol64(h ^ ((uint64_t)*p * _XX64_P5), 11) * _XX64_P1;
                p++;
        }
        h ^= h >> 33;
        h *= _XX64_P2;
        h ^= h >> 29;
        h *= _XX64_P3;
        h ^= h >> 32;
        return h;
}

/* ---- Murmur3-32 -------------------------------------------- */

static uint32_t _mm3_fmix(uint32_t h) {
        h ^= h >> 16;
        h *= 0x85ebca6bu;
        h ^= h >> 13;
        h *= 0xc2b2ae35u;
        h ^= h >> 16;
        return h;
}
uint32_t murmur3_32(const void* data, size_t len, uint32_t seed) {
        const uint8_t* p  = (const uint8_t*)data;
        size_t         nb = len / 4;
        uint32_t       h  = seed;
        size_t         i;
        for (i = 0; i < nb; i++) {
                uint32_t k;
                memcpy(&k, p + i * 4, 4);
                k *= 0xcc9e2d51u;
                k = bit_rol32(k, 15);
                k *= 0x1b873593u;
                h ^= k;
                h = bit_rol32(h, 13);
                h = h * 5 + 0xe6546b64u;
        }
        {
                const uint8_t* t = p + nb * 4;
                uint32_t       k = 0;
                switch (len & 3) {
                        case 3:
                                k ^= (uint32_t)t[2] << 16; /* fall through */
                        case 2:
                                k ^= (uint32_t)t[1] << 8; /* fall through */
                        case 1:
                                k ^= (uint32_t)t[0];
                                k *= 0xcc9e2d51u;
                                k = bit_rol32(k, 15);
                                k *= 0x1b873593u;
                                h ^= k;
                        default:
                                break;
                }
        }
        h ^= (uint32_t)len;
        return _mm3_fmix(h);
}

#        endif /* BAREMATH_IMPLEMENTATION */
#endif         /* BAREMATH_H */
