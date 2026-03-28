#define BARESTD_IMPLEMENTATION
#define BARETIME_IMPLEMENTATION
#define BARETEST_IMPLEMENTATION
#define BAREENCODING_IMPLEMENTATION
#include "baretime.h"
#include "bareencoding.h"
#include "baretest.h"

int main(void) {
        Arena* a = arena_new(KB(32));

        /* ---- Base64 ---- */
        TEST("base64 encode/decode") {
                Str enc = base64_encode(a, str_lit("Hello, World!"));
                ASSERT_STR(enc, str_lit("SGVsbG8sIFdvcmxkIQ=="));
                Str dec = base64_decode(a, enc);
                ASSERT_STR(dec, str_lit("Hello, World!"));
        }
        TEST("base64 edge cases") {
                Str e0 = base64_encode(a, str_lit(""));
                ASSERT_EQ(size_t, e0.len, 0);
                ASSERT_STR(base64_encode(a, str_lit("A")), str_lit("QQ=="));
                ASSERT_STR(base64_encode(a, str_lit("AB")), str_lit("QUI="));
                ASSERT_STR(base64_encode(a, str_lit("ABC")), str_lit("QUJD"));
        }
        TEST("base64 url-safe no padding") {
                uint8_t bin[] = {0xFB, 0xFF, 0xFE};
                Str     enc   = base64_url_encode(a, str_buf(bin, 3));
                /* No '+', '/', or '=' characters */
                size_t i;
                for (i = 0; i < enc.len; i++) {
                        char c = enc.ptr[i];
                        ASSERT(c != '+' && c != '/' && c != '=');
                }
                /* Round-trip */
                Str dec = base64_decode(a, enc);
                ASSERT_EQ(size_t, dec.len, 3);
                ASSERT_MEM_EQ(dec.ptr, bin, 3);
        }
        TEST("base64 invalid input") {
                Str bad = base64_decode(a, str_lit("!!!"));
                ASSERT(str_is_null(bad));
        }

        /* ---- Hex ---- */
        TEST("hex encode/decode round-trip") {
                uint8_t bytes[] = {0xDE, 0xAD, 0xBE, 0xEF};
                Str     enc     = hex_encode(a, str_buf(bytes, 4));
                ASSERT_STR(enc, str_lit("deadbeef"));
                Str upper = hex_encode_upper(a, str_buf(bytes, 4));
                ASSERT_STR(upper, str_lit("DEADBEEF"));
                Str dec = hex_decode(a, enc);
                ASSERT_EQ(size_t, dec.len, 4);
                ASSERT_MEM_EQ(dec.ptr, bytes, 4);
        }
        TEST("hex 0x prefix") {
                uint8_t expected[] = {0xDE, 0xAD};
                Str     dec        = hex_decode(a, str_lit("0xDEAD"));
                ASSERT_EQ(size_t, dec.len, 2);
                ASSERT_MEM_EQ(dec.ptr, expected, 2);
        }
        TEST("hex invalid input") {
                ASSERT(str_is_null(hex_decode(a, str_lit("xyz"))));
                ASSERT(str_is_null(
                    hex_decode(a, str_lit("abc")))); /* odd length */
        }

        /* ---- Percent encoding ---- */
        TEST("pct_encode unreserved unchanged") {
                Str s = str_lit(
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz012345"
                    "6789-._~");
                ASSERT_STR(pct_encode(a, s), s);
        }
        TEST("pct_encode spaces and special chars") {
                ASSERT_STR(pct_encode(a, str_lit("hello world")),
                           str_lit("hello%20world"));
                ASSERT_STR(pct_encode(a, str_lit("a/b")), str_lit("a%2Fb"));
                ASSERT_STR(pct_encode_path(a, str_lit("a/b")), str_lit("a/b"));
        }
        TEST("pct_decode round-trip") {
                Str original = str_lit("hello world/foo+bar");
                Str enc      = pct_encode(a, original);
                Str dec      = pct_decode(a, enc);
                ASSERT_STR(dec, original);
        }
        TEST("pct_decode plus-as-space") {
                Str dec = pct_decode(a, str_lit("hello+world"));
                ASSERT_STR(dec, str_lit("hello world"));
        }

        /* ---- HTML escaping ---- */
        TEST("html_escape") {
                Str esc = html_escape(a, str_lit("<b>hi & \"bye\"</b>"));
                /* check key entities appear */
                ASSERT(str_find(esc, str_lit("&lt;")) >= 0);
                ASSERT(str_find(esc, str_lit("&gt;")) >= 0);
                ASSERT(str_find(esc, str_lit("&amp;")) >= 0);
                ASSERT(str_find(esc, str_lit("&quot;")) >= 0);
                /* raw dangerous chars must not appear as literals */
                ASSERT(str_find(esc, str_lit("<")) < 0);
                ASSERT(str_find(esc, str_lit(">")) < 0);
        }
        TEST("html round-trip") {
                Str original =
                    str_lit("<div class=\"foo\">hello & world</div>");
                Str esc   = html_escape(a, original);
                Str unesc = html_unescape(a, esc);
                ASSERT_STR(unesc, original);
        }
        TEST("html numeric entities") {
                Str s = html_unescape(a, str_lit("&#65;&#66;&#67;"));
                ASSERT_STR(s, str_lit("ABC"));
        }

        /* ---- Numeric assertions demo ---- */
        TEST("ASSERT_NEAR demo") {
                ASSERT_NEAR(3.14159265, 3.14159, 0.0001);
                ASSERT_NEAR(0.1 + 0.2, 0.3, 1e-10);
        }
        TEST("comparison operators") {
                ASSERT_LT(int, 1, 2);
                ASSERT_LE(int, 2, 2);
                ASSERT_GT(int, 3, 2);
                ASSERT_GE(int, 2, 2);
                ASSERT_NE(int, 1, 2);
        }
        TEST("null checks") {
                char* p = NULL;
                char  buf[4];
                ASSERT_NULL(p);
                ASSERT_NOT_NULL(buf);
        }

        /* ---- SKIP demo ---- */
        TEST("future feature") {
                SKIP("not implemented yet");
        }

        arena_free(a);
        return baretest_finish();
}
