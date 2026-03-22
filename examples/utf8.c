#define BARESTD_IMPLEMENTATION
#define BAREUTF8_IMPLEMENTATION
#include <stdio.h>
#include <string.h>

#include "bareutf8.h"

int main(void) {
        /* --- 1. utf8_decode: single rune --- */
        const char* src = "A";
        Rune        r;
        int         n = utf8_decode(src, strlen(src), &r);
        printf("decode 'A': cp=U+%04X  width=%d  consumed=%d\n",
               r.cp,
               utf8_width(r),
               n);

        /* 2-byte sequence */
        const char* e_acute = "\xC3\xA9"; /* e-acute: U+00E9 */
        n                   = utf8_decode(e_acute, 2, &r);
        printf("decode U+00E9: cp=0x%04X  width=%d  consumed=%d\n",
               r.cp,
               utf8_width(r),
               n);

        /* 3-byte sequence: euro sign U+20AC */
        const char* euro = "\xE2\x82\xAC";
        n                = utf8_decode(euro, 3, &r);
        printf("decode euro U+20AC: cp=0x%04X  width=%d  consumed=%d\n",
               r.cp,
               utf8_width(r),
               n);

        /* 4-byte sequence: U+1F600 (if emoji display works) */
        const char* emoji = "\xF0\x9F\x98\x80";
        n                 = utf8_decode(emoji, 4, &r);
        printf("decode U+1F600: cp=0x%05X  width=%d  consumed=%d\n",
               r.cp,
               utf8_width(r),
               n);

        /* --- 2. utf8_encode --- */
        Rune enc;
        int  ew = utf8_encode(&enc, 0x20AC); /* euro sign */
        printf("encode U+20AC: width=%d  bytes=[%02X %02X %02X]\n",
               ew,
               enc.bytes[0],
               enc.bytes[1],
               enc.bytes[2]);

        Rune encA;
        utf8_encode(&encA, 'A');
        printf("encode 'A': cp=%d  byte=0x%02X\n", encA.cp, encA.bytes[0]);

        /* Invalid codepoint produces U+FFFD */
        Rune bad_enc;
        utf8_encode(&bad_enc, 0xD800); /* surrogate -- invalid */
        printf("encode surrogate: cp=U+%04X (FFFD=%d)\n",
               bad_enc.cp,
               bad_enc.cp == RUNE_ERROR);

        /* --- 3. utf8_width --- */
        Rune w1, w2, w3, w4;
        utf8_encode(&w1, 0x41);    /* A */
        utf8_encode(&w2, 0xE9);    /* e-acute */
        utf8_encode(&w3, 0x20AC);  /* euro */
        utf8_encode(&w4, 0x1F600); /* emoji */
        printf("widths: 0x41=%d  0xE9=%d  0x20AC=%d  0x1F600=%d\n",
               utf8_width(w1),
               utf8_width(w2),
               utf8_width(w3),
               utf8_width(w4));

        /* --- 4. utf8_len: count runes not bytes --- */
        /* ASCII only */
        Str ascii = str_lit("hello");
        printf(
            "ascii  byte_len=%zu  rune_len=%zu\n", ascii.len, utf8_len(ascii));

        /* Multi-byte */
        /* "cafe" with e-acute: 5 bytes, 4 runes */
        const char cafe_raw[] = {'c', 'a', 'f', '\xC3', '\xA9', 0};
        Str        cafe       = str_from_c(cafe_raw);
        printf("cafe   byte_len=%zu  rune_len=%zu\n", cafe.len, utf8_len(cafe));

        /* --- 5. Utf8Iter: iterate rune by rune --- */
        Str      text = str_lit("hi!");
        Utf8Iter it   = utf8_iter(text);
        Rune     cur;
        printf("iterate 'hi!': ");
        while (utf8_next(&it, &cur)) {
                printf("U+%04X ", cur.cp);
        }
        printf("\n");

        /* --- 6. Error handling: invalid byte --- */
        const char bad_raw[] = {(char)0xFF, 'A', 0};
        int        nb        = utf8_decode(bad_raw, 2, &r);
        printf("invalid 0xFF: cp=U+%04X (FFFD=%d)  consumed=%d\n",
               r.cp,
               r.cp == RUNE_ERROR,
               nb);

        /* Truncated multi-byte */
        const char truncated[] = {(char)0xE2,
                                  0}; /* start of 3-byte, missing 2 */
        utf8_decode(truncated, 1, &r);
        printf("truncated 3-byte: cp=U+%04X (FFFD=%d)\n",
               r.cp,
               r.cp == RUNE_ERROR);

        /* Overlong encoding */
        const char overlong[] = {(char)0xC0, (char)0x80, 0}; /* overlong NUL */
        utf8_decode(overlong, 2, &r);
        printf("overlong: cp=U+%04X (FFFD=%d)\n", r.cp, r.cp == RUNE_ERROR);

        /* Empty input */
        int ne = utf8_decode("", 0, &r);
        printf("empty input: cp=%d (EOF=%d)  returned=%d\n",
               r.cp,
               r.cp == RUNE_EOF,
               ne);

        /* --- 7. Rune predicates --- */
        Rune rA, r3, rSpace, rEuro, rComma;
        utf8_encode(&rA, 'A');
        utf8_encode(&r3, '3');
        utf8_encode(&rSpace, ' ');
        utf8_encode(&rEuro, 0x20AC);
        utf8_encode(&rComma, ',');

        printf("\npredicates:\n");
        printf("is_valid('A')    = %d\n", rune_is_valid(rA));
        printf("is_valid(U+D800) = %d\n", rune_is_valid((Rune){.cp = 0xD800}));
        printf("is_ascii('A')    = %d\n", rune_is_ascii(rA));
        printf("is_ascii(euro)   = %d\n", rune_is_ascii(rEuro));
        printf("is_digit('3')    = %d\n", rune_is_digit(r3));
        printf("is_digit('A')    = %d\n", rune_is_digit(rA));
        printf("is_upper('A')    = %d\n", rune_is_upper(rA));
        printf("is_lower('A')    = %d\n", rune_is_lower(rA));
        printf("is_alpha('A')    = %d\n", rune_is_alpha(rA));
        printf("is_alnum('3')    = %d\n", rune_is_alnum(r3));
        printf("is_space(' ')    = %d\n", rune_is_space(rSpace));
        printf("is_punct(',')    = %d\n", rune_is_punct(rComma));

        /* --- 8. Case conversion --- */
        Rune rz, rZ;
        utf8_encode(&rz, 'z');
        utf8_encode(&rZ, 'Z');
        Rune up   = rune_to_upper(rz);
        Rune down = rune_to_lower(rZ);
        printf("\nto_upper('z') = '%c'\n", (char)up.cp);
        printf("to_lower('Z') = '%c'\n", (char)down.cp);

        /* Non-ASCII unchanged */
        Rune up_euro = rune_to_upper(rEuro);
        printf("to_upper(euro) unchanged: %d\n", up_euro.cp == rEuro.cp);

        /* --- 9. Constants --- */
        printf("\nconstants:\n");
        printf("RUNE_ERROR = U+%04X\n", RUNE_ERROR);
        printf("RUNE_EOF   = %d\n", RUNE_EOF);
        printf("RUNE_MAX   = U+%05X\n", RUNE_MAX);

        printf("done.\n");
        return 0;
}
