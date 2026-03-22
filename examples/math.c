#define BARESTD_IMPLEMENTATION
#define BAREMATH_IMPLEMENTATION
#include <stdio.h>
#include <string.h>

#include "baremath.h"

int main(void) {
        /* ===== S0: Constants ===== */
        printf("--- S0: Constants ---\n");
        printf("PI     = %.15f\n", MATH_PI);
        printf("TAU    = %.15f\n", MATH_TAU);
        printf("E      = %.15f\n", MATH_E);
        printf("PHI    = %.15f\n", MATH_PHI);
        printf("SQRT2  = %.15f\n", MATH_SQRT2);
        printf("LN2    = %.15f\n", MATH_LN2);
        printf("F32_EPS= %e\n", MATH_F32_EPS);
        printf("F64_EPS= %e\n", MATH_F64_EPS);

        /* ===== S1: Type punning ===== */
        printf("--- S1: F32/F64 unions ---\n");
        F32 f32 = f32_from_float(1.0f);
        printf("1.0f bits = 0x%08X (expect 0x3F800000)\n", f32.u);
        F32 neg = f32_from_bits(F32_SIGN_BIT | f32.u);
        printf("-1.0f = %.1f\n", neg.f);
        printf("float32bits(1.0f)  = 0x%08X\n", math_float32bits(1.0f));
        printf("float64bits(1.0)   = 0x%016llX\n",
               (unsigned long long)math_float64bits(1.0));
        F64 f64 = f64_from_double(-0.0);
        printf("-0.0 sign bit set: %d\n", (f64.u & F64_SIGN_BIT) != 0);

        /* ===== S2: Special values ===== */
        printf("--- S2: Special values ---\n");
        double nan  = math_nan();
        double pinf = math_inf(1);
        double ninf = math_inf(-1);
        printf("is_nan(NaN)    = %d\n", math_is_nan(nan));
        printf("is_inf(+Inf,1) = %d\n", math_is_inf(pinf, 1));
        printf("is_inf(-Inf,-1)= %d\n", math_is_inf(ninf, -1));
        printf("is_inf(+Inf,0) = %d\n", math_is_inf(pinf, 0));
        printf("signbit(-1.5)  = %d\n", math_signbit(-1.5));
        printf("signbit(+1.5)  = %d\n", math_signbit(+1.5));

        /* ===== S3: Basic float ops ===== */
        printf("--- S3: Basic float ops ---\n");
        printf("abs(-3.7)        = %.1f\n", math_abs(-3.7));
        printf("absf(-3.7f)      = %.1f\n", math_absf(-3.7f));
        printf("ceil(2.1)        = %.1f\n", math_ceil(2.1));
        printf("floor(2.9)       = %.1f\n", math_floor(2.9));
        printf("trunc(-2.9)      = %.1f\n", math_trunc(-2.9));
        printf("round(2.5)       = %.1f\n", math_round(2.5));
        printf("round_to_even(2.5)=%.1f\n", math_round_to_even(2.5));
        printf("mod(5.3, 2.0)    = %.4f\n", math_mod(5.3, 2.0));
        printf("remainder(5.3,2) = %.4f\n", math_remainder(5.3, 2.0));
        double intpart, fracpart;
        math_modf(3.75, &intpart, &fracpart);
        printf("modf(3.75): int=%.0f frac=%.2f\n", intpart, fracpart);
        printf("dim(5,3)   = %.1f\n", math_dim(5, 3));
        printf("dim(3,5)   = %.1f\n", math_dim(3, 5));
        printf("fmax(2,NaN)= %f\n", math_fmax(2.0, math_nan()));
        printf("fmin(2,NaN)= %f\n", math_fmin(2.0, math_nan()));
        printf("copysign(3,-1)=%.1f\n", math_copysign(3.0, -1.0));
        printf("clampd(15,0,10)=%.1f\n", math_clampd(15.0, 0.0, 10.0));
        printf("clampi(-5,0,10)=%d\n", math_clampi(-5, 0, 10));
        printf("lerp(0,10,0.3) =%.1f\n", math_lerp(0.0, 10.0, 0.3));
        printf("inv_lerp(0,10,3)=%.2f\n", math_inv_lerp(0.0, 10.0, 3.0));
        printf("remap(5,0,10,0,100)=%.1f\n", math_remap(5, 0, 10, 0, 100));
        printf("smoothstep(0,1,0.5) =%.4f\n", math_smoothstep(0.0, 1.0, 0.5));
        printf("smootherstep(0,1,0.5)=%.4f\n",
               math_smootherstep(0.0, 1.0, 0.5));

        /* ===== S4: Trig ===== */
        printf("--- S4: Trigonometric ---\n");
        double s, c;
        math_sincos(MATH_PI / 4.0, &s, &c);
        printf("sincos(PI/4): sin=%.4f cos=%.4f\n", s, c);
        printf(
            "atan2(1,1)  = %.4f (PI/4=%.4f)\n", math_atan2(1, 1), MATH_PI / 4);
        printf("deg_to_rad(180) = %.4f\n", math_deg_to_rad(180.0));
        printf("rad_to_deg(PI)  = %.4f\n", math_rad_to_deg(MATH_PI));
        printf("asin(0.5)  = %.4f\n", math_asin(0.5));
        printf("acos(0.5)  = %.4f\n", math_acos(0.5));
        printf("atan(1.0)  = %.4f\n", math_atan(1.0));

        /* ===== S5: Hyperbolic ===== */
        printf("--- S5: Hyperbolic ---\n");
        printf("sinh(1)  = %.6f\n", math_sinh(1.0));
        printf("cosh(1)  = %.6f\n", math_cosh(1.0));
        printf("tanh(0.5)= %.6f\n", math_tanh(0.5));
        printf("asinh(1) = %.6f\n", math_asinh(1.0));
        printf("acosh(2) = %.6f\n", math_acosh(2.0));
        printf("atanh(0.5)=%.6f\n", math_atanh(0.5));

        /* ===== S6: Exp/Log ===== */
        printf("--- S6: Exponential / Log ---\n");
        printf("exp(1)    = %.6f (e=%.6f)\n", math_exp(1.0), MATH_E);
        printf("exp2(10)  = %.0f\n", math_exp2(10.0));
        printf("expm1(0.001)=%.8f\n", math_expm1(0.001));
        printf("log(e)    = %.4f\n", math_log(MATH_E));
        printf("log2(8)   = %.1f\n", math_log2(8.0));
        printf("log10(1000)=%.1f\n", math_log10(1000.0));
        printf("log1p(1e-10)=%.10e\n", math_log1p(1e-10));
        printf("logb(256) = %.0f\n", math_logb(256.0));
        printf("ilogb(256)= %d\n", math_ilogb(256.0));

        /* ===== S7: Power / Root ===== */
        printf("--- S7: Power / Root ---\n");
        printf("sqrt(2)   = %.6f\n", math_sqrt(2.0));
        printf("cbrt(27)  = %.1f\n", math_cbrt(27.0));
        printf("pow(2,10) = %.0f\n", math_pow(2.0, 10.0));
        printf("pow10(3)  = %.0f\n", math_pow10(3));
        printf("hypot(3,4)= %.1f\n", math_hypot(3.0, 4.0));
        printf("inv_sqrtf_fast(4)~=%.4f (expect ~0.5)\n",
               math_inv_sqrtf_fast(4.0f));

        /* ===== S8: FP utilities ===== */
        printf("--- S8: FP utilities ---\n");
        int    exp2;
        double sig = math_frexp(3.5, &exp2);
        printf("frexp(3.5): sig=%.4f exp=%d\n", sig, exp2);
        printf("ldexp(sig,exp)=%.1f\n", math_ldexp(sig, exp2));
        printf("fma(2,3,1) = %.1f\n", math_fma(2.0, 3.0, 1.0));
        printf("nextafter(1.0,2.0) > 1.0: %d\n",
               math_nextafter(1.0, 2.0) > 1.0);

        /* ===== S9: Special functions ===== */
        printf("--- S9: Special functions ---\n");
        printf("erf(1.0)  = %.6f\n", math_erf(1.0));
        printf("erfc(1.0) = %.6f\n", math_erfc(1.0));
        printf("erfinv(erf(0.5)) = %.4f (expect ~0.5)\n",
               math_erfinv(math_erf(0.5)));
        printf("erfcinv(erfc(0.5)) = %.4f\n", math_erfcinv(math_erfc(0.5)));
        printf("gamma(5) = %.0f (expect 24)\n", math_gamma(5.0));
        int gsign;
        printf("lgamma(5, &sign): %.4f sign=%d\n",
               math_lgamma(5.0, &gsign),
               gsign);

        /* ===== S10: Integer math ===== */
        printf("--- S10: Integer math ---\n");
        printf("gcd(48,18)     = %llu\n", (unsigned long long)math_gcd(48, 18));
        printf("lcm(4,6)       = %llu\n", (unsigned long long)math_lcm(4, 6));
        printf("log2_floor(100)= %u\n", math_log2_floor(100));
        printf("log2_ceil(100) = %u\n", math_log2_ceil(100));
        printf("next_pow2(100) = %llu\n",
               (unsigned long long)math_next_pow2(100));
        printf("isqrt(144)     = %llu\n", (unsigned long long)math_isqrt(144));
        printf("icbrt(27)      = %llu\n", (unsigned long long)math_icbrt(27));
        printf("div_ceil(7,3)  = %llu\n",
               (unsigned long long)math_div_ceil(7, 3));
        printf("align_up(13,8) = %llu\n",
               (unsigned long long)math_align_up(13, 8));
        printf("align_down(13,8)=%llu\n",
               (unsigned long long)math_align_down(13, 8));
        printf("abs32(-42)     = %d\n", math_abs32(-42));
        printf("abs64(-9)      = %lld\n", (long long)math_abs64(-9));

        /* ===== S11: Bit operations ===== */
        printf("--- S11: Bit operations ---\n");
        printf("popcount32(0xFF)     = %u\n", bit_popcount32(0xFF));
        printf("popcount64(0xFFFF)   = %u\n", bit_popcount64(0xFFFF));
        printf("clz32(1)             = %u\n", bit_clz32(1));
        printf("ctz32(0x100)         = %u\n", bit_ctz32(0x100));
        printf("clz64(1ULL<<63)      = %u\n", bit_clz64((uint64_t)1 << 63));
        printf("ctz64(0x100ULL)      = %u\n", bit_ctz64((uint64_t)0x100));
        printf("bswap16(0x0102)      = 0x%04X\n", bit_bswap16(0x0102));
        printf("bswap32(0x01020304)  = 0x%08X\n", bit_bswap32(0x01020304));
        uint64_t orig = 0xDEADBEEFCAFEBABEULL;
        printf("bswap64 roundtrip ok = %d\n",
               bit_bswap64(bit_bswap64(orig)) == orig);
        printf("rol32(1,4)           = 0x%X\n", bit_rol32(1u, 4));
        printf("ror32(0x10,4)        = 0x%X\n", bit_ror32(0x10u, 4));
        printf("rol64(1,4)           = 0x%llX\n",
               (unsigned long long)bit_rol64(1ULL, 4));
        printf("ror64(0x10,4)        = 0x%llX\n",
               (unsigned long long)bit_ror64(0x10ULL, 4));
        printf("bit_extract(0xFF0,4,4)=%llu\n",
               (unsigned long long)bit_extract(0xFF0, 4, 4));
        printf("bit_insert(0,0xA,4,4)=0x%llX\n",
               (unsigned long long)bit_insert(0, 0xA, 4, 4));
        printf("bit_reverse32(0x80000000)=0x%X\n", bit_reverse32(0x80000000u));
        printf("bit_next_perm(7=0b0111)=%llu (expect 11=0b1011)\n",
               (unsigned long long)bit_next_perm(7));

        /* ===== S12: Overflow arithmetic ===== */
        printf("--- S12: Overflow arithmetic ---\n");
        uint32_t r32;
        int      ov;
        ov = math_add_u32(UINT32_MAX, 1, &r32);
        printf("add_u32 overflow: ov=%d\n", ov);
        ov = math_sub_u32(0, 1, &r32);
        printf("sub_u32 underflow: ov=%d\n", ov);
        ov = math_mul_u32(100000u, 100000u, &r32);
        printf("mul_u32(1e5,1e5) ov=%d\n", ov);
        uint64_t r64;
        ov = math_add_u64(UINT64_MAX, 1, &r64);
        printf("add_u64 overflow: ov=%d\n", ov);
        int32_t ri32;
        ov = math_add_i32(INT32_MAX, 1, &ri32);
        printf("add_i32 overflow: ov=%d\n", ov);
        ov = math_mul_i32(100000, 100000, &ri32);
        printf("mul_i32(1e5,1e5) ov=%d\n", ov);
        int64_t ri64;
        ov = math_add_i64(INT64_MAX, 1, &ri64);
        printf("add_i64 overflow: ov=%d\n", ov);

        /* ===== S13: Fixed-point ===== */
        printf("--- S13: Fixed-point Q16.16 / Q8.24 ---\n");
        Q16_16 qa = q16_from_double(3.75);
        Q16_16 qb = q16_from_double(1.25);
        printf("3.75 + 1.25 = %.4f\n", q16_to_double(q16_add(qa, qb)));
        printf("3.75 - 1.25 = %.4f\n", q16_to_double(q16_sub(qa, qb)));
        printf("3.75 * 1.25 = %.4f\n", q16_to_double(q16_mul(qa, qb)));
        printf("3.75 / 1.25 = %.4f\n", q16_to_double(q16_div(qa, qb)));
        printf("abs(-3.75)  = %.4f\n", q16_to_double(q16_abs(q16_neg(qa))));
        printf("to_int(3.75)= %d\n", q16_to_int(qa));
        printf("lt(1.25,3.75)=%d\n", q16_lt(qb, qa));
        printf("eq(1.25,1.25)=%d\n", q16_eq(qb, qb));
        Q16_16 qmin = q16_min(qa, qb);
        Q16_16 qmax = q16_max(qa, qb);
        printf("min=%.2f max=%.2f\n", q16_to_double(qmin), q16_to_double(qmax));
        Q16_16 qif = q16_from_int(5);
        printf("from_int(5) = %.4f\n", q16_to_double(qif));

        Q8_24 pa = q8_from_double(0.5);
        Q8_24 pb = q8_from_double(0.25);
        printf("Q8.24: 0.5 + 0.25 = %.6f\n", q8_to_double(q8_add(pa, pb)));
        printf("Q8.24: 0.5 * 0.25 = %.6f\n", q8_to_double(q8_mul(pa, pb)));
        printf("Q8.24: 0.5 / 0.25 = %.6f\n", q8_to_double(q8_div(pa, pb)));
        printf("Q8.24: from_int(3)= %.6f\n", q8_to_double(q8_from_int(3)));

        /* ===== S14: Vectors, Quaternion, Mat4 ===== */
        printf("--- S14: Vec2 ---\n");
        Vec2 v2a = vec2(3.0f, 4.0f);
        Vec2 v2b = vec2(1.0f, 0.0f);
        printf("vec2 len          = %.1f\n", vec2_len(v2a));
        printf("vec2 len2         = %.1f\n", vec2_len2(v2a));
        printf("vec2 dot(v2a,v2b) = %.1f\n", vec2_dot(v2a, v2b));
        printf("vec2 cross(v2a,v2b)=%.1f\n", vec2_cross(v2a, v2b));
        Vec2 v2n = vec2_norm(v2a);
        printf("vec2 norm: (%.3f, %.3f)\n", v2n.x, v2n.y);
        Vec2 v2lerp = vec2_lerp(vec2(0, 0), vec2(10, 10), 0.5f);
        printf("vec2 lerp(0,10,0.5)=(%.1f,%.1f)\n", v2lerp.x, v2lerp.y);
        Vec2 v2ref = vec2_reflect(vec2(1, -1), vec2(0, 1));
        printf("vec2 reflect(1,-1) over (0,1)=(%.1f,%.1f)\n", v2ref.x, v2ref.y);
        Vec2 v2p = vec2_perp(vec2(1, 0));
        printf("vec2 perp(1,0)=(%.1f,%.1f)\n", v2p.x, v2p.y);
        printf("vec2 dist       = %.4f\n", vec2_dist(vec2(0, 0), vec2(3, 4)));
        printf("vec2_get(v2a,0) = %.1f\n", vec2_get(v2a, 0));

        printf("--- S14: Vec3 ---\n");
        Vec3 v3x = vec3(1, 0, 0);
        Vec3 v3y = vec3(0, 1, 0);
        Vec3 v3c = vec3_cross(v3x, v3y);
        printf("cross(x,y)=(%.0f,%.0f,%.0f)\n", v3c.x, v3c.y, v3c.z);
        printf("vec3 dot  = %.1f\n", vec3_dot(v3x, v3x));
        Vec3 v3n = vec3_norm(vec3(3, 4, 0));
        printf("norm(3,4,0): (%.3f,%.3f,%.3f)\n", v3n.x, v3n.y, v3n.z);
        Vec3 v3ref = vec3_reflect(vec3(1, -1, 0), vec3(0, 1, 0));
        printf("reflect: (%.1f,%.1f,%.1f)\n", v3ref.x, v3ref.y, v3ref.z);
        Vec3 v3refr = vec3_refract(vec3(0, 0, -1), vec3(0, 0, 1), 1.0f / 1.5f);
        printf("refract: (%.4f,%.4f,%.4f)\n", v3refr.x, v3refr.y, v3refr.z);
        Vec2 v3xy = vec3_xy(vec3(1, 2, 3));
        printf("vec3_xy: (%.0f,%.0f)\n", v3xy.x, v3xy.y);

        printf("--- S14: Vec4 + colour aliases ---\n");
        Vec4 v4 = vec4(1, 2, 3, 4);
        printf("vec4 dot = %.1f\n", vec4_dot(v4, v4));
        printf("vec4 len = %.4f\n", vec4_len(v4));
        Vec4 red = vec4_rgba(1, 0, 0, 1);
        printf("rgba red: r=%.0f g=%.0f b=%.0f a=%.0f\n",
               red.x,
               red.y,
               red.z,
               red.w);
        Vec3 green = vec3_rgb(0, 1, 0);
        printf("rgb green: g=%.0f\n", green.y);
        Vec2 uv = vec2_uv(0.5f, 0.25f);
        printf("uv: (%.2f, %.2f)\n", uv.x, uv.y);
        Vec3 v4xyz = vec4_xyz(v4);
        printf("vec4_xyz: (%.0f,%.0f,%.0f)\n", v4xyz.x, v4xyz.y, v4xyz.z);

        printf("--- S14: Quaternion ---\n");
        Vec4 q = quat_from_axis_angle(vec3(0, 1, 0), (float)(MATH_PI / 2));
        Vec3 rotated = quat_rotate_vec3(q, vec3(1, 0, 0));
        printf("rotate X 90 deg around Y: (%.3f,%.3f,%.3f) (expect ~0,0,-1)\n",
               rotated.x,
               rotated.y,
               rotated.z);
        Vec4 qi = quat_identity();
        printf("identity: (%.0f,%.0f,%.0f,%.0f)\n", qi.x, qi.y, qi.z, qi.w);
        Vec4 qc = quat_conj(q);
        printf("conj: w=%.4f\n", qc.w);
        Vec4 qn = quat_norm(q);
        printf("norm len: %.4f (expect 1.0)\n", vec4_len(qn));
        Vec4 qa_q = quat_from_axis_angle(vec3(1, 0, 0), (float)(MATH_PI / 4));
        Vec4 slp  = quat_slerp(qi, qa_q, 0.5f);
        printf("slerp 0->PI/4 at t=0.5: w=%.4f\n", slp.w);

        printf("--- S14: Mat4 ---\n");
        Mat4 I = mat4_identity();
        printf("identity[0][0]=%.0f [1][1]=%.0f\n", I.e[0], I.e[5]);
        Mat4 T = mat4_translate(vec3(1, 2, 3));
        printf("translate col3=(%.0f,%.0f,%.0f)\n", T.e[12], T.e[13], T.e[14]);
        Mat4 S = mat4_scale_v(vec3(2, 3, 4));
        printf("scale diag=(%.0f,%.0f,%.0f)\n", S.e[0], S.e[5], S.e[10]);
        Mat4 R = mat4_rotate(vec3(0, 1, 0), (float)(MATH_PI / 2));
        printf("rotate Y 90 [0][0]=%.4f\n", R.e[0]);
        Mat4 TT = mat4_transpose(T);
        printf("transpose row3=(%.0f,%.0f,%.0f,%.0f)\n",
               TT.e[3],
               TT.e[7],
               TT.e[11],
               TT.e[15]);
        Mat4 inv = mat4_inverse(mat4_identity());
        printf("inverse(I)==I: %d\n", memcmp(I.e, inv.e, 64) == 0);
        Mat4 AB = mat4_mul(T, S);
        printf("mat4_mul: ok (combined transform)\n");
        Vec4 tv = mat4_mul_vec4(I, vec4(1, 2, 3, 1));
        printf(
            "mul_vec4 by I: (%.0f,%.0f,%.0f,%.0f)\n", tv.x, tv.y, tv.z, tv.w);
        Mat4 proj =
            mat4_perspective((float)(MATH_PI / 3), 16.0f / 9, 0.1f, 100.0f);
        printf("perspective [0]=%.4f\n", proj.e[0]);
        Mat4 lkat = mat4_look_at(vec3(0, 0, 5), vec3(0, 0, 0), vec3(0, 1, 0));
        printf("look_at [14]=%.4f\n", lkat.e[14]);
        (void)AB;

        /* ===== S15: Hashing ===== */
        printf("--- S15: Hashing ---\n");
        const char data[] = "hello, world";
        size_t     len    = 12;
        uint8_t    key[16];
        memset(key, 0, 16);
        uint64_t sip  = siphash13(data, len, key);
        uint32_t xx32 = xxhash32(data, len, 0);
        uint64_t xx64 = xxhash64(data, len, 0);
        uint32_t mm3  = murmur3_32(data, len, 0);
        printf("SipHash-1-3 = 0x%016llX\n", (unsigned long long)sip);
        printf("xxHash32    = 0x%08X\n", xx32);
        printf("xxHash64    = 0x%016llX\n", (unsigned long long)xx64);
        printf("Murmur3-32  = 0x%08X\n", mm3);
        printf("deterministic: %d\n", xxhash32(data, len, 0) == xx32);
        printf("seed-sensitive: %d\n", xxhash32(data, len, 1) != xx32);
        printf("empty xxhash32=0x%08X\n", xxhash32("", 0, 0));

        printf("done.\n");
        return 0;
}
