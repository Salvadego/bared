#define BARESTD_IMPLEMENTATION
#define BARESTRS_IMPLEMENTATION
#include <ctype.h>
#include <stdio.h>

#include "barestrs.h"

int main(void) {
        Arena* a = arena_new(KB(32));
        Str    s = str_lit("Hello, World! Hello, C!");

        /* --- 1. Search functions --- */
        printf("last_find('Hello')       = %td\n",
               str_last_find(s, str_lit("Hello")));
        printf("index_byte(',')          = %td\n", str_index_byte(s, ','));
        printf("last_index_byte(',')     = %td\n", str_last_index_byte(s, ','));
        printf("index_any('aeiou')       = %td\n",
               str_index_any(s, str_lit("aeiou")));
        printf("last_index_any('aeiou')  = %td\n",
               str_last_index_any(s, str_lit("aeiou")));
        printf("index_func(isupper)      = %td\n", str_index_func(s, isupper));
        printf("last_index_func(isupper) = %td\n",
               str_last_index_func(s, isupper));

        /* --- 2. Predicate functions --- */
        printf("\npredicates:\n");
        printf("contains_any('xyz')    = %d\n",
               str_contains_any(s, str_lit("xyz")));
        printf("contains_any('WC')     = %d\n",
               str_contains_any(s, str_lit("WC")));
        printf("contains_func(isdigit) = %d\n", str_contains_func(s, isdigit));
        printf("count('Hello')         = %zu\n",
               str_count(s, str_lit("Hello")));
        printf("count('l')             = %zu\n", str_count(s, str_lit("l")));
        printf("count('') (rune count) = %zu\n", str_count(s, str_lit("")));
        printf("equal_fold(ABC,abc)    = %d\n",
               str_equal_fold(str_lit("ABC"), str_lit("abc")));
        printf("equal_fold(ABC,ABD)    = %d\n",
               str_equal_fold(str_lit("ABC"), str_lit("ABD")));

        /* --- 3. Trim --- */
        printf("\ntrim:\n");
        Str padded = str_lit("  \ttrim me\t  ");
        printf("trim_space:       \"" StrFmt "\"\n",
               StrArgs(str_trim_space(padded)));
        printf("trim_left_func:   \"" StrFmt "\"\n",
               StrArgs(str_trim_left_func(padded, isspace)));
        printf("trim_right_func:  \"" StrFmt "\"\n",
               StrArgs(str_trim_right_func(padded, isspace)));
        printf("trim_func:        \"" StrFmt "\"\n",
               StrArgs(str_trim_func(padded, isspace)));
        printf("trim_prefix(foo): \"" StrFmt "\"\n",
               StrArgs(str_trim_prefix(str_lit("foobar"), str_lit("foo"))));
        printf("trim_suffix(bar): \"" StrFmt "\"\n",
               StrArgs(str_trim_suffix(str_lit("foobar"), str_lit("bar"))));
        printf("trim_prefix(miss):\"" StrFmt "\"\n",
               StrArgs(str_trim_prefix(str_lit("foobar"), str_lit("baz"))));

        /* --- 4. Cut --- */
        printf("\ncut:\n");
        StrCut c1 = str_cut(str_lit("user@host"), str_lit("@"));
        printf("cut '@': found=%d before=\"" StrFmt "\" after=\"" StrFmt "\"\n",
               c1.found,
               StrArgs(c1.before),
               StrArgs(c1.after));

        StrCut c2 = str_cut(str_lit("no-separator"), str_lit("@"));
        printf("cut missing: found=%d before=\"" StrFmt "\"\n",
               c2.found,
               StrArgs(c2.before));

        StrCut cp = str_cut_prefix(str_lit("--verbose"), str_lit("--"));
        printf("cut_prefix '--': found=%d after=\"" StrFmt "\"\n",
               cp.found,
               StrArgs(cp.after));

        StrCut cs = str_cut_suffix(str_lit("archive.tar.gz"), str_lit(".gz"));
        printf("cut_suffix '.gz': found=%d before=\"" StrFmt "\"\n",
               cs.found,
               StrArgs(cs.before));

        /* --- 5. Split --- */
        printf("\nsplit:\n");
        Slice(Str) sp = str_split(a, str_lit("a,b,,c"), str_lit(","));
        printf("split count=%zu\n", slice_len(sp));
        for (size_t i = 0; i < slice_len(sp); i++)
                printf("  [%zu]=\"" StrFmt "\"\n", i, StrArgs(sp[i]));

        Slice(Str) sn = str_split_n(a, str_lit("a:b:c:d"), str_lit(":"), 3);
        printf("split_n(3): count=%zu  [2]=\"" StrFmt "\" (remainder)\n",
               slice_len(sn),
               StrArgs(sn[2]));

        Slice(Str) sa = str_split_after(a, str_lit("a,b,c"), str_lit(","));
        printf("split_after[0]=\"" StrFmt "\"  [2]=\"" StrFmt "\"\n",
               StrArgs(sa[0]),
               StrArgs(sa[2]));

        Slice(Str) wds = str_fields(a, str_lit("  hello   world  bare  "));
        printf("fields count=%zu\n", slice_len(wds));
        for (size_t i = 0; i < slice_len(wds); i++)
                printf("  [%zu]=\"" StrFmt "\"\n", i, StrArgs(wds[i]));

        Slice(Str) wdf = str_fields_func(a, str_lit("1,2,,3"), ispunct);
        printf("fields_func count=%zu\n", slice_len(wdf));

        /* --- 6. Build --- */
        printf("\nbuild:\n");
        Str joined = str_join(a, wds, str_lit(", "));
        printf("join = \"" StrFmt "\"\n", StrArgs(joined));

        Str rep = str_repeat(a, str_lit("ab"), 4);
        printf("repeat 'ab'x4 = \"" StrFmt "\"\n", StrArgs(rep));

        Str repa = str_replace_all(a, s, str_lit("Hello"), str_lit("Hi"));
        printf("replace_all = \"" StrFmt "\"\n", StrArgs(repa));

        Str rep1 = str_replace(a, s, str_lit("Hello"), str_lit("Hey"), 1);
        printf("replace(n=1)= \"" StrFmt "\"\n", StrArgs(rep1));

        Str rep0 = str_replace(a, s, str_lit("Hello"), str_lit("Hey"), 0);
        printf("replace(n=0)= \"" StrFmt "\" (unchanged)\n", StrArgs(rep0));

        printf("to_upper = \"" StrFmt "\"\n",
               StrArgs(str_to_upper(a, str_lit("hello world"))));
        printf("to_lower = \"" StrFmt "\"\n",
               StrArgs(str_to_lower(a, str_lit("HELLO WORLD"))));
        printf("title    = \"" StrFmt "\"\n",
               StrArgs(str_title(a, str_lit("the quick brown fox"))));

        /* Edge cases */
        Str empty_join = str_join(a, slice_make(a, Str, 1), str_lit(","));
        printf("join empty slice = \"" StrFmt "\"\n", StrArgs(empty_join));
        Str zero_rep = str_repeat(a, str_lit("abc"), 0);
        printf("repeat 0 times   = \"" StrFmt "\"\n", StrArgs(zero_rep));

        arena_free(a);
        printf("done.\n");
        return 0;
}
