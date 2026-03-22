#define BARESTD_IMPLEMENTATION
#define BAREBUILDER_IMPLEMENTATION
#include <stdio.h>

#include "barebuilder.h"

int main(void) {
        Arena* a = arena_new(KB(32));

        /* --- 1. Basic building: char, str, cstr --- */
        StrBuilder sb = sb_make(a, 64);
        sb_write_str(&sb, str_lit("Hello"));
        sb_write_char(&sb, ',');
        sb_write_char(&sb, ' ');
        sb_write_cstr(&sb, "world");
        sb_write_char(&sb, '!');
        Str view = sb_to_str(&sb);
        printf("view: \"" StrFmt "\"\n", StrArgs(view));

        /* --- 2. sb_write_fmt --- */
        sb_write_fmt(&sb, " (%d + %d = %d)", 2, 3, 5);
        Str view2 = sb_to_str(&sb);
        printf("with fmt: \"" StrFmt "\"\n", StrArgs(view2));

        /* --- 3. sb_to_cstr: copy into arena, release scratch --- */
        char* result = sb_to_cstr(a, &sb);
        printf("cstr: \"%s\"\n", result);
        printf("builder reset after to_cstr: len=%zu\n", slice_len(sb.buf));

        /* --- 4. sb_write_repeat --- */
        sb_write_repeat(&sb, '=', 20);
        Str line = sb_to_str(&sb);
        printf("repeat '=' x20: \"" StrFmt "\"\n", StrArgs(line));
        sb_reset(&sb);

        /* --- 5. sb_reset: discard without copying --- */
        sb_write_cstr(&sb, "will be discarded");
        printf("before reset: len=%zu\n", slice_len(sb.buf));
        sb_reset(&sb);
        printf("after  reset: len=%zu\n", slice_len(sb.buf));

        /* --- 6. Growing beyond initial capacity --- */
        StrBuilder small = sb_make(a, 4);
        for (int i = 0; i < 50; i++)
                sb_write_char(&small, (char)('a' + i % 26));
        printf("grew small builder: len=%zu\n", slice_len(small.buf));
        sb_reset(&small);

        /* --- 7. sb_to_str is a non-owning view (scratch still live) --- */
        StrBuilder sb2 = sb_make(a, 16);
        sb_write_cstr(&sb2, "view is non-owning");
        Str v = sb_to_str(&sb2);
        printf("non-owning view: \"" StrFmt "\"\n", StrArgs(v));
        /* sb2 scratch is still live here; ptr points into scratch region */
        sb_reset(&sb2);

        /* --- 8. Multiple fmt calls in sequence --- */
        StrBuilder  doc     = sb_make(a, 256);
        const char* lines[] = {"first line", "second line", "third line", NULL};
        for (int i = 0; lines[i]; i++) {
                sb_write_fmt(&doc, "line %d: %s\n", i + 1, lines[i]);
        }
        char* text = sb_to_cstr(a, &doc);
        printf("document:\n%s", text);

        /* --- 9. Large format string (exceeds 256-byte stack buffer in impl)
         * --- */
        StrBuilder big = sb_make(a, 32);
        sb_write_fmt(&big, "%0300d", 0); /* 300-char zero-padded int */
        printf("big fmt len=%zu\n", slice_len(big.buf));
        sb_reset(&big);

        arena_free(a);
        printf("done.\n");
        return 0;
}
