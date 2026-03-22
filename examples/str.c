#define BARESTD_IMPLEMENTATION
#include <ctype.h>
#include <stdio.h>

#include "barestd.h"

int main(void) {
        Arena* a = arena_new(KB(32));

        /* --- 1. Construction macros --- */
        Str lit  = str_lit("hello, world");
        Str from = str_from_c("c-string");
        Str buf  = str_buf("raw bytes", 9);
        Str null = str_null();
        printf("lit  len=%zu  text=\"" StrFmt "\"\n", lit.len, StrArgs(lit));
        printf("from len=%zu\n", from.len);
        printf("buf  len=%zu\n", buf.len);
        printf(
            "is_null=%d  is_empty=%d\n", str_is_null(null), str_is_empty(null));

        /* --- 2. Comparison --- */
        printf("eq(abc,abc)=%d\n", str_eq(str_lit("abc"), str_lit("abc")));
        printf("has_prefix: %d\n", str_has_prefix(lit, str_lit("hello")));
        printf("has_suffix: %d\n", str_has_suffix(lit, str_lit("world")));

        /* --- 3. Search --- */
        Str hay = str_lit("the quick brown fox");
        printf("find 'brown' at: %td\n", str_find(hay, str_lit("brown")));
        printf("find 'cat'   at: %td\n", str_find(hay, str_lit("cat")));

        /* --- 4. Slice --- */
        Str sub = str_slice(hay, 4, 9);
        printf("slice [4,9) = \"" StrFmt "\"\n", StrArgs(sub));

        /* --- 5. Trim --- */
        Str padded = str_lit("   hello   ");
        Str ws     = str_lit(" \t\n");
        printf("trim_left:  \"" StrFmt "\"\n",
               StrArgs(str_trim_left(padded, ws)));
        printf("trim_right: \"" StrFmt "\"\n",
               StrArgs(str_trim_right(padded, ws)));
        printf("trim:       \"" StrFmt "\"\n", StrArgs(str_trim(padded, ws)));

        /* --- 6. Tokeniser --- */
        Str rest = str_lit("a,bb,ccc,dddd");
        Str tok;
        printf("tokens: ");
        while (!str_is_null(tok = str_next_token(&rest, str_lit(",")))) {
                printf("\"" StrFmt "\" ", StrArgs(tok));
        }
        printf("\n");

        /* --- 7. consume_while / consume_until --- */
        Str src = str_lit("   42abc");
        str_consume_while(&src, isspace);
        Str digits = str_consume_while(&src, isdigit);
        printf("digits = \"" StrFmt "\"\n", StrArgs(digits));
        Str alpha = str_consume_while(&src, isalpha);
        printf("alpha  = \"" StrFmt "\"\n", StrArgs(alpha));

        Str sentence = str_lit("hello world");
        Str word     = str_consume_until(&sentence, isspace);
        printf("consume_until space: \"" StrFmt "\"\n", StrArgs(word));

        /* --- 8. Numeric parsing (non-consuming) --- */
        int64_t  i64 = 0;
        uint64_t u64 = 0;
        double   f64 = 0;
        bool     b   = false;
        str_to_i64(str_lit("-123"), &i64);
        printf("i64 = %lld\n", (long long)i64);
        str_to_u64(str_lit("0xFF"), &u64);
        printf("u64 = %llu (hex)\n", (unsigned long long)u64);
        str_to_f64(str_lit("3.14159"), &f64);
        printf("f64 = %.5f\n", f64);
        str_to_bool(str_lit("true"), &b);
        printf("bool = %d\n", (int)b);
        str_to_bool(str_lit("false"), &b);
        printf("bool = %d\n", (int)b);
        str_to_bool(str_lit("1"), &b);
        printf("bool = %d\n", (int)b);

        /* --- 9. Consuming numeric variants (advance the pointer) --- */
        Str     multi = str_lit("  10  20  30");
        int64_t v0 = 0, v1 = 0, v2 = 0;
        str_consume_i64(&multi, &v0);
        str_consume_i64(&multi, &v1);
        str_consume_i64(&multi, &v2);
        printf("multi-parse: %lld %lld %lld\n",
               (long long)v0,
               (long long)v1,
               (long long)v2);

        /* --- 10. Arena allocation helpers --- */
        char* cstr = str_to_cstr(a, str_lit("nul-terminated"));
        printf("cstr[14]='%c' (NUL=%d)\n", cstr[14], cstr[14] == '\0');

        Str cloned = str_clone(a, str_lit("owned copy"));
        printf("cloned = \"" StrFmt "\"\n", StrArgs(cloned));

        Str fmt = str_fmt(a, "result = %d + %d = %d", 2, 3, 2 + 3);
        printf("fmt = \"" StrFmt "\"\n", StrArgs(fmt));

        /* --- 11. str_is_empty edge cases --- */
        printf("empty str_lit: is_empty=%d\n", str_is_empty(str_lit("")));
        printf("null str:      is_empty=%d\n", str_is_empty(str_null()));

        arena_free(a);
        printf("done.\n");
        return 0;
}
