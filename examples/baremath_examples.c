
/*
 * baremath_examples.c -- common usage patterns for baremath.h
 *
 */

#define BAREMATH_IMPLEMENTATION
#include "baremath.h"
#include <stdio.h>

/* ================================================================
 *  Helpers
 * ================================================================ */

static void sep(const char *title) {
    printf("\n--- %s ---\n", title);
}

/* ================================================================
 *  1. Basic float ops
 * ================================================================ */

static void example_basic(void) {
    sep("Basic float ops");

    printf("abs(-3.5)        = %.2f\n", math_abs(-3.5));
    printf("ceil(1.2)        = %.2f\n", math_ceil(1.2));
    printf("floor(1.8)       = %.2f\n", math_floor(1.8));
    printf("round(1.5)       = %.2f\n", math_round(1.5));
    printf("trunc(1.9)       = %.2f\n", math_trunc(1.9));
    printf("clamp(5, 0, 3)   = %.2f\n", math_clampd(5.0, 0.0, 3.0));
    printf("lerp(0, 10, 0.3) = %.2f\n", math_lerp(0.0, 10.0, 0.3));
    printf("dim(3, 5)        = %.2f\n", math_dim(3.0, 5.0));  /* max(3-5, 0) */
    printf("dim(5, 3)        = %.2f\n", math_dim(5.0, 3.0));  /* max(5-3, 0) */

    /* remap a value from one range to another */
    /* e.g. map a health value 0-100 to a bar width 0-200 */
    double health     = 75.0;
    double bar_width  = math_remap(health, 0.0, 100.0, 0.0, 200.0);
    printf("remap 75 health -> bar width: %.1f px\n", bar_width);

    /* smooth transition -- no sudden jumps at edges */
    printf("smoothstep(0,1, 0.0) = %.3f\n", math_smoothstep(0,1, 0.0));
    printf("smoothstep(0,1, 0.5) = %.3f\n", math_smoothstep(0,1, 0.5));
    printf("smoothstep(0,1, 1.0) = %.3f\n", math_smoothstep(0,1, 1.0));
    printf("smootherstep(0,1, 0.0) = %.3f\n", math_smootherstep(0,1, 0.0));
    printf("smootherstep(0,1, 0.5) = %.3f\n", math_smootherstep(0,1, 0.5));
    printf("smootherstep(0,1, 1.0) = %.3f\n", math_smootherstep(0,1, 1.0));
}

/* ================================================================
 *  2. Special values -- NaN, Inf, sign
 * ================================================================ */

static void example_special(void) {
    sep("Special values");

    double pos_inf = math_inf(1);
    double neg_inf = math_inf(-1);
    double nan     = math_nan();

    printf("inf(+1) is inf:  %d\n", math_is_inf(pos_inf, 1));
    printf("inf(-1) is -inf: %d\n", math_is_inf(neg_inf, -1));
    printf("nan is nan:      %d\n", math_is_nan(nan));
    printf("1.0 is nan:      %d\n", math_is_nan(1.0));
    printf("signbit(-0.5):   %d\n", math_signbit(-0.5));
    printf("signbit(+0.5):   %d\n", math_signbit(0.5));

    /* NaN propagates through arithmetic -- use to detect bad input */
    double result = nan + 5.0;
    printf("nan + 5 is nan:  %d\n", math_is_nan(result));
}

/* ================================================================
 *  3. Type punning -- inspect float bits directly
 * ================================================================ */

static void example_punning(void) {
    sep("Type punning");

    float  f = 1.0f;
    double d = 1.0;

    printf("1.0f bits:   0x%08X\n", math_float32bits(f));
    printf("1.0  bits:   0x%016llX\n", (unsigned long long)math_float64bits(d));

    /* reconstruct from bits */
    float  back_f = math_float32frombits(0x3F800000u);
    double back_d = math_float64frombits(UINT64_C(0x3FF0000000000000));
    printf("from bits f: %.1f\n", back_f);
    printf("from bits d: %.1f\n", back_d);

    /* fast inverse sqrt using bit trick */
    float x   = 4.0f;
    float inv  = math_inv_sqrtf_fast(x);  /* ~0.5, approximation */
    printf("inv_sqrt(4) approx: %.4f  exact: %.4f\n", inv, 1.0f/sqrtf(x));

    /* inspect sign/exponent/mantissa directly */
    F32 bits;
    bits.f = -2.5f;
    int   sign     = (bits.u >> 31) & 1;
    int   exponent = ((bits.u >> 23) & 0xFF) - 127;
    int   mantissa = bits.u & 0x7FFFFF;
    printf("-2.5: sign=%d exp=%d mantissa=0x%06X\n", sign, exponent, mantissa);
}

/* ================================================================
 *  4. Trig -- angles, directions
 * ================================================================ */

static void example_trig(void) {
    sep("Trigonometry");

    double deg  = 45.0;
    double rad  = math_deg_to_rad(deg);
    printf("45 deg in radians: %.4f\n", rad);
    printf("back to degrees:   %.1f\n", math_rad_to_deg(rad));

    double s, c;
    math_sincos(rad, &s, &c);
    printf("sin(45 deg): %.4f\n", s);
    printf("cos(45 deg): %.4f\n", c);

    /* atan2 -- get angle of a direction vector */
    double dx = 1.0, dy = 1.0;
    double angle = math_atan2(dy, dx);
    printf("angle of (1,1): %.4f rad = %.1f deg\n",
           angle, math_rad_to_deg(angle));
}

/* ================================================================
 *  5. Bit operations
 * ================================================================ */

static void example_bits(void) {
    sep("Bit operations");

    uint32_t x = 0b10110100;
    printf("x          = 0x%02X (%u)\n", x, x);
    printf("popcount   = %u\n",  bit_popcount32(x));
    printf("clz        = %u\n",  bit_clz32(x));
    printf("ctz        = %u\n",  bit_ctz32(x));
    printf("bswap32    = 0x%08X\n", bit_bswap32(0x01020304u));
    printf("rol32(1,3) = %u\n",  bit_rol32(1u, 3));    /* 8 */
    printf("ror32(8,3) = %u\n",  bit_ror32(8u, 3));    /* 1 */

    /* pack/unpack multiple values into one integer */
    uint64_t packed = 0;
    packed = bit_insert(packed, 255, 0,  8);   /* red   in bits  0-7  */
    packed = bit_insert(packed, 128, 8,  8);   /* green in bits  8-15 */
    packed = bit_insert(packed,  64, 16, 8);   /* blue  in bits 16-23 */
    printf("packed rgb: 0x%06llX\n", (unsigned long long)packed);
    printf("red:   %llu\n", (unsigned long long)bit_extract(packed, 0,  8));
    printf("green: %llu\n", (unsigned long long)bit_extract(packed, 8,  8));
    printf("blue:  %llu\n", (unsigned long long)bit_extract(packed, 16, 8));

    /* next power of two */
    printf("next_pow2(100) = %llu\n", (unsigned long long)math_next_pow2(100));
    printf("log2_floor(100)= %u\n",   math_log2_floor(100));
}

/* ================================================================
 *  6. Overflow-checked arithmetic
 * ================================================================ */

static void example_overflow(void) {
    sep("Overflow-checked arithmetic");

    uint32_t result;
    int overflow;

    overflow = math_add_u32(0xFFFFFFFEu, 1u, &result);
    printf("0xFFFFFFFE + 1 = 0x%X (overflow=%d)\n", result, overflow);

    overflow = math_add_u32(0xFFFFFFFFu, 1u, &result);
    printf("0xFFFFFFFF + 1 = 0x%X (overflow=%d)\n", result, overflow);

    overflow = math_mul_u32(100000u, 100000u, &result);
    printf("100000 * 100000 overflow=%d\n", overflow);

    overflow = math_mul_u32(1000u, 1000u, &result);
    printf("1000 * 1000 = %u (overflow=%d)\n", result, overflow);

    /* useful for buffer size calculations */
    size_t count = 1000000, elem_size = 8;
    uint64_t total;
    if (math_mul_u64((uint64_t)count, (uint64_t)elem_size, &total))
        printf("allocation would overflow!\n");
    else
        printf("safe allocation: %llu bytes\n", (unsigned long long)total);
}

/* ================================================================
 *  7. Fixed-point arithmetic
 * ================================================================ */

static void example_fixed_point(void) {
    sep("Fixed-point Q16.16");

    Q16_16 a = q16_from_double(3.14);
    Q16_16 b = q16_from_double(2.0);
    Q16_16 c = q16_mul(a, b);
    Q16_16 d = q16_div(a, b);

    printf("3.14 raw:       % d\n",       a.raw);
    printf("3.14 to double: % f\n",       q16_to_double(a));
    printf("3.14 * 2.0:     % .5f\n",     q16_to_double(c));
    printf("3.14 / 2.0:     % .5f\n",     q16_to_double(d));
    printf("floor(3.14):    % d\n",       q16_to_int(a));

    /* useful for audio, embedded, anything without FPU */
    Q16_16 volume = q16_from_double(0.75);
    Q16_16 sample = q16_from_int(32767);   /* max 16-bit PCM */
    Q16_16 scaled = q16_mul(sample, volume);
    printf("PCM sample * 0.75 volume: %d\n", q16_to_int(scaled));
}

/* ================================================================
 *  8. Vectors -- 2D game example
 * ================================================================ */

static void example_vectors_2d(void) {
    sep("Vectors 2D -- game movement");

    Vec2 player_pos = vec2(10.0f, 5.0f);
    Vec2 enemy_pos  = vec2(14.0f, 8.0f);

    /* direction from player to enemy */
    Vec2  to_enemy  = vec2_sub(enemy_pos, player_pos);
    float distance  = vec2_len(to_enemy);
    Vec2  direction = vec2_norm(to_enemy);

    printf("player:    (%.1f, %.1f)\n", player_pos.xy.x, player_pos.xy.y);
    printf("enemy:     (%.1f, %.1f)\n", enemy_pos.xy.x, enemy_pos.xy.y);
    printf("distance:  %.2f\n",         distance);
    printf("direction: (%.2f, %.2f)\n", direction.xy.x, direction.xy.y);

    /* move player toward enemy at speed 2.0 */
    float speed    = 2.0f;
    Vec2  velocity = vec2_scale(direction, speed);
    Vec2  new_pos  = vec2_add(player_pos, velocity);
    printf("new pos:   (%.2f, %.2f)\n", new_pos.xy.x, new_pos.xy.y);

    /* reflect a bullet off a wall (normal pointing right) */
    Vec2 bullet_vel  = vec2(1.0f, -1.0f);
    Vec2 wall_normal = vec2(1.0f,  0.0f);   /* vertical wall */
    Vec2 reflected   = vec2_reflect(bullet_vel, wall_normal);
    printf("reflected: (%.1f, %.1f)\n", reflected.xy.x, reflected.xy.y);

    /* perpendicular -- left-hand normal of a direction */
    Vec2 perp = vec2_perp(direction);
    printf("perp:      (%.2f, %.2f)\n", perp.xy.x, perp.xy.y);
    perp = vec2_perp(perp);
    printf("perp2:      (%.2f, %.2f)\n", perp.xy.x, perp.xy.y);
    perp = vec2_perp(perp);
    printf("perp3:      (%.2f, %.2f)\n", perp.xy.x, perp.xy.y);
    perp = vec2_perp(perp);
    printf("perp4:      (%.2f, %.2f)\n", perp.xy.x, perp.xy.y);
    perp = vec2_perp(perp);
}

/* ================================================================
 *  9. Vectors -- 3D graphics example
 * ================================================================ */

static void example_vectors_3d(void) {
    sep("Vectors 3D -- lighting");

    /* Lambertian (diffuse) lighting */
    Vec3  surface_normal = vec3_norm(vec3(0.0f, 1.0f, 0.0f)); /* pointing up */
    Vec3  light_dir      = vec3_norm(vec3(1.0f, 1.0f, 0.0f)); /* upper right */
    float diffuse        = math_fmax(vec3_dot(surface_normal, light_dir), 0.0);
    printf("diffuse light intensity: %.3f\n", diffuse);

    /* cross product -- surface normal from two edges */
    Vec3 edge1  = vec3(1.0f, 0.0f, 0.0f);
    Vec3 edge2  = vec3(0.0f, 1.0f, 0.0f);
    Vec3 normal = vec3_norm(vec3_cross(edge1, edge2));
    printf("computed normal: (%.1f, %.1f, %.1f)\n",
           normal.xyz.x, normal.xyz.y, normal.xyz.z);

    /* sub-vector extraction */
    Vec3 pos = vec3(1.0f, 2.0f, 3.0f);
    Vec2 xy  = vec3_xy(pos);
    printf("pos.xy: (%.1f, %.1f)\n", xy.xy.x, xy.xy.y);
}

/* ================================================================
 *  10. Vec4 -- colour and homogeneous coords
 * ================================================================ */

static void example_vec4(void) {
    sep("Vec4 -- colour / homogeneous coords");

    /* colour arithmetic */
    Vec4 red  = vec4(1.0f, 0.0f, 0.0f, 1.0f);
    Vec4 blue = vec4(0.0f, 0.0f, 1.0f, 1.0f);
    Vec4 mix  = vec4_lerp(red, blue, 0.5f);  /* purple */

    printf("red:   (%.1f %.1f %.1f %.1f)\n",
           red.rgba.r, red.rgba.g, red.rgba.b, red.rgba.a);
    printf("blue:  (%.1f %.1f %.1f %.1f)\n",
           blue.rgba.r, blue.rgba.g, blue.rgba.b, blue.rgba.a);
    printf("mix:   (%.1f %.1f %.1f %.1f)\n",
           mix.rgba.r, mix.rgba.g, mix.rgba.b, mix.rgba.a);

    /* extract sub-vectors */
    Vec4 pos = vec4(1.0f, 2.0f, 3.0f, 1.0f);
    Vec3 xyz = vec4_xyz(pos);
    Vec2 lo  = vec4_lo(pos);   /* x, y */
    Vec2 hi  = vec4_hi(pos);   /* z, w */
    printf("pos.xyz: (%.1f %.1f %.1f)\n", xyz.xyz.x, xyz.xyz.y, xyz.xyz.z);
    printf("pos.lo:  (%.1f %.1f)\n",      lo.xy.x,   lo.xy.y);
    printf("pos.hi:  (%.1f %.1f)\n",      hi.xy.x,   hi.xy.y);

    /* array access for generic loops */
    printf("components: ");
    int i;
    for (i = 0; i < 4; i++) printf("%.1f ", pos.e[i]);
    printf("\n");
}

/* ================================================================
 *  11. Quaternion -- rotating a vector
 * ================================================================ */

static void example_quat(void) {
    sep("Quaternion -- rotation");

    /* rotate (1,0,0) by 90 degrees around Z axis -> should give (0,1,0) */
    Vec3 axis   = vec3(0.0f, 0.0f, 1.0f);
    Vec4 q      = quat_from_axis_angle(axis, math_deg_to_rad(90.0));
    Vec3 vec    = vec3(1.0f, 0.0f, 0.0f);
    Vec3 rotated = quat_rotate_vec3(q, vec);
    printf("(1,0,0) rotated 90 deg around Z: (%.2f %.2f %.2f)\n",
           rotated.xyz.x, rotated.xyz.y, rotated.xyz.z);

    /* interpolate between two orientations */
    Vec4 q1  = quat_identity();
    Vec4 q2  = quat_from_axis_angle(axis, math_deg_to_rad(90.0));
    Vec4 mid = quat_slerp(q1, q2, 0.5f);
    Vec3 v2  = quat_rotate_vec3(mid, vec);
    printf("halfway (45 deg):               (%.2f %.2f %.2f)\n",
           v2.xyz.x, v2.xyz.y, v2.xyz.z);
}

/* ================================================================
 *  12. Mat4 -- transform pipeline
 * ================================================================ */

static void example_mat4(void) {
    sep("Mat4 -- transform");

    /* translation */
    Vec3 offset = vec3(5.0f, 0.0f, 0.0f);
    Mat4 T      = mat4_translate(offset);
    Vec4 p      = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    Vec4 tp     = mat4_mul_vec4(T, p);
    printf("translate (0,0,0) by (5,0,0): (%.1f %.1f %.1f)\n",
           tp.e[0], tp.e[1], tp.e[2]);

    /* scale */
    Vec3 s  = vec3(2.0f, 2.0f, 2.0f);
    Mat4 S  = mat4_scale_v(s);
    Vec4 sp = mat4_mul_vec4(S, vec4(1.0f, 1.0f, 1.0f, 1.0f));
    printf("scale (1,1,1) by 2:           (%.1f %.1f %.1f)\n",
           sp.e[0], sp.e[1], sp.e[2]);

    /* rotate 90 deg around Y */
    Vec3 y_axis = vec3(0.0f, 1.0f, 0.0f);
    Mat4 R      = mat4_rotate(y_axis, math_deg_to_rad(90.0f));
    Vec4 rp     = mat4_mul_vec4(R, vec4(1.0f, 0.0f, 0.0f, 1.0f));
    printf("rotate (1,0,0) 90 deg Y:      (%.2f %.2f %.2f)\n",
           rp.e[0], rp.e[1], rp.e[2]);  /* expect (~0, 0, -1) */

    /* camera: perspective projection */
    Mat4 proj = mat4_perspective(
        math_deg_to_rad(60.0f),  /* fov */
        16.0f / 9.0f,            /* aspect */
        0.1f,                    /* near */
        1000.0f                  /* far */
    );
    Vec3 eye    = vec3(0.0f, 0.0f, 5.0f);
    Vec3 center = vec3(0.0f, 0.0f, 0.0f);
    Vec3 up     = vec3(0.0f, 1.0f, 0.0f);
    Mat4 view   = mat4_look_at(eye, center, up);
    Mat4 vp     = mat4_mul(proj, view);

    /* transform world-space point (0,0,0) to clip space */
    Vec4 world_pt  = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    Vec4 clip_pt   = mat4_mul_vec4(vp, world_pt);
    /* divide by w for NDC */
    float ndc_x = clip_pt.e[0] / clip_pt.e[3];
    float ndc_y = clip_pt.e[1] / clip_pt.e[3];
    printf("origin in NDC (should be 0,0): (%.2f, %.2f)\n", ndc_x, ndc_y);
}

/* ================================================================
 *  13. Hashing -- building a simple set
 * ================================================================ */

static void example_hashing(void) {
    sep("Hashing");

    /* deterministic: same input -> same output */
    const char *words[] = { "hello", "world", "foo", "bar" };
    int i;
    for (i = 0; i < 4; i++) {
        uint32_t h32 = xxhash32(words[i], strlen(words[i]), 0);
        uint64_t h64 = xxhash64(words[i], strlen(words[i]), 0);
        printf("%-8s  xx32=0x%08X  xx64=0x%016llX\n",
               words[i], h32, (unsigned long long)h64);
    }

    /* SipHash with a secret key -- for untrusted input */
    uint8_t secret_key[16] = {
        0x00,0x01,0x02,0x03, 0x04,0x05,0x06,0x07,
        0x08,0x09,0x0A,0x0B, 0x0C,0x0D,0x0E,0x0F
    };
    uint64_t safe = siphash13("user input", 10, secret_key);
    printf("siphash13: 0x%016llX\n", (unsigned long long)safe);

    /* Murmur3 -- simple and fast */
    uint32_t mm = murmur3_32("hello", 5, 42);
    printf("murmur3:   0x%08X\n", mm);
}

/* ================================================================
 *  14. GCD / LCM -- scheduling example
 * ================================================================ */

static void example_integer_math(void) {
    sep("Integer math");

    /* GCD: find common update period for two systems */
    uint64_t physics_hz  = 120;  /* updates per second */
    uint64_t render_hz   = 60;
    uint64_t common      = math_gcd(physics_hz, render_hz);
    printf("GCD(120, 60) = %llu\n", (unsigned long long)common);

    /* LCM: find when two periodic events coincide */
    uint64_t event_a = 4;  /* fires every 4 ticks */
    uint64_t event_b = 6;  /* fires every 6 ticks */
    uint64_t sync    = math_lcm(event_a, event_b);
    printf("LCM(4, 6)    = %llu  (both fire at tick %llu)\n",
           (unsigned long long)sync, (unsigned long long)sync);

    printf("isqrt(99)    = %llu\n", (unsigned long long)math_isqrt(99));
    printf("icbrt(999)   = %llu\n", (unsigned long long)math_icbrt(999));
    printf("next_pow2(65)= %llu\n", (unsigned long long)math_next_pow2(65));
}

/* ================================================================
 *  main
 * ================================================================ */

int main(void) {
    example_basic();
    example_special();
    example_punning();
    example_trig();
    example_bits();
    example_overflow();
    example_fixed_point();
    example_vectors_2d();
    example_vectors_3d();
    example_vec4();
    example_quat();
    example_mat4();
    example_hashing();
    example_integer_math();
    printf("\n");
    return 0;
}
