#define BARESTD_IMPLEMENTATION
#define BAREPATH_IMPLEMENTATION
#include <stdio.h>

#include "barepath.h"

int main(void) {
        Arena* a = arena_new(KB(16));
        Str    p = str_lit("src/foo/bar.c");

        /* --- 1. Decomposition (no allocation) --- */
        printf("path:    \"" StrFmt "\"\n", StrArgs(p));
        printf("dir:     \"" StrFmt "\"\n", StrArgs(path_dir(p)));
        printf("base:    \"" StrFmt "\"\n", StrArgs(path_base(p)));
        printf("ext:     \"" StrFmt "\"\n", StrArgs(path_ext(p)));
        printf("stem:    \"" StrFmt "\"\n", StrArgs(path_stem(p)));

        /* Edge cases for decomposition */
        printf("dir('/')       = \"" StrFmt "\"\n",
               StrArgs(path_dir(str_lit("/"))));
        printf("dir('a')       = \"" StrFmt "\"\n",
               StrArgs(path_dir(str_lit("a"))));
        printf("base('.')      = \"" StrFmt "\"\n",
               StrArgs(path_base(str_lit("."))));
        printf("base('/a/b/')  = \"" StrFmt "\"\n",
               StrArgs(path_base(str_lit("/a/b/"))));
        printf("ext('.hidden') = \"" StrFmt "\" (no ext)\n",
               StrArgs(path_ext(str_lit(".hidden"))));
        printf("ext('no_ext')  = \"" StrFmt "\" (empty)\n",
               StrArgs(path_ext(str_lit("no_ext"))));
        printf("stem('.hidden')= \"" StrFmt "\"\n",
               StrArgs(path_stem(str_lit(".hidden"))));

        /* --- 2. Predicates --- */
        printf("\npredicates:\n");
        printf("is_abs('/usr')  = %d\n", path_is_abs(str_lit("/usr")));
        printf("is_abs('rel')   = %d\n", path_is_abs(str_lit("rel")));
        printf("is_abs('')      = %d\n", path_is_abs(str_lit("")));
        printf("is_clean('a/b') = %d\n", path_is_clean(str_lit("a/b")));
        printf("is_clean('a//b')= %d\n", path_is_clean(str_lit("a//b")));
        printf("is_clean('a/../b')=%d\n", path_is_clean(str_lit("a/../b")));
        printf("is_clean('a\\b')= %d\n", path_is_clean(str_lit("a\\b")));

        /* --- 3. path_join --- */
        printf("\npath_join:\n");
        Str j1 = path_join(a, str_lit("src/foo"), str_lit("bar.c"));
        Str j2 = path_join(a, str_lit("src/foo/"), str_lit("bar.c"));
        Str j3 = path_join(
            a, str_lit("src/foo"), str_lit("/abs")); /* abs replaces */
        Str j4 = path_join(a, str_lit(""), str_lit("bar.c"));
        Str j5 = path_join(a, str_lit("src"), str_lit(""));
        printf("join('src/foo','bar.c')   = \"" StrFmt "\"\n", StrArgs(j1));
        printf("join('src/foo/','bar.c')  = \"" StrFmt "\"\n", StrArgs(j2));
        printf("join('src/foo','/abs')    = \"" StrFmt "\" (abs wins)\n",
               StrArgs(j3));
        printf("join('','bar.c')          = \"" StrFmt "\"\n", StrArgs(j4));
        printf("join('src','')            = \"" StrFmt "\"\n", StrArgs(j5));

        /* --- 4. path_join_slice --- */
        printf("\npath_join_slice:\n");
        Slice(Str) parts = slice_make(a, Str, 4);
        Str pa = str_lit("a"), pb = str_lit("b"), pc = str_lit("c");
        slice_put(parts, pa);
        slice_put(parts, pb);
        slice_put(parts, pc);
        Str joined = path_join_slice(a, parts);
        printf("join_slice(a,b,c) = \"" StrFmt "\"\n", StrArgs(joined));

        /* --- 5. path_clean --- */
        printf("\npath_clean:\n");
        struct {
                const char* in;
        } cases[] = {
            {"a//b/../c"},
            {"./x"},
            {""},
            {"/a/b/../../c"},
            {"../../a"},
            {"a/./b/./c"},
            {"a/b/c/.."},
            {"/"},
            {"src\\foo"}, /* backslash normalised */
        };
        for (int i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
                Str cl = path_clean(a, str_from_c(cases[i].in));
                printf("clean(\"%s\") = \"" StrFmt "\"\n",
                       cases[i].in,
                       StrArgs(cl));
        }

        /* --- 6. path_rel --- */
        printf("\npath_rel:\n");
        Str r1 = path_rel(a, str_lit("src/foo/bar.c"), str_lit("src"));
        Str r2 = path_rel(a, str_lit("src/foo/bar.c"), str_lit("other"));
        printf("rel('src/foo/bar.c','src')   = \"" StrFmt "\"\n", StrArgs(r1));
        printf("rel('src/foo/bar.c','other') = \"" StrFmt "\" (not a prefix)\n",
               StrArgs(r2));

        /* --- 7. path_with_ext --- */
        printf("\npath_with_ext:\n");
        Str we1 = path_with_ext(a, str_lit("foo/bar.c"), str_lit(".h"));
        Str we2 = path_with_ext(a, str_lit("foo/bar"), str_lit(".o"));
        Str we3 =
            path_with_ext(a, str_lit("foo/bar.c"), str_lit("")); /* strip ext */
        printf("with_ext(bar.c, .h) = \"" StrFmt "\"\n", StrArgs(we1));
        printf("with_ext(bar,   .o) = \"" StrFmt "\"\n", StrArgs(we2));
        printf("with_ext(bar.c, '') = \"" StrFmt "\"\n", StrArgs(we3));

        arena_free(a);
        printf("done.\n");
        return 0;
}
