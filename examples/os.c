#define BARESTD_IMPLEMENTATION
#define BAREOS_IMPLEMENTATION
#define BARETIME_IMPLEMENTATION
#include "bareos.h"
#include <stdio.h>
#include <string.h>


int main(void) {
        Arena* a = arena_new(KB(32));

        /* --- 1. os_write_file / os_read_file --- */
        Str  path    = str_lit("/tmp/bare_test.txt");
        Str  content = str_lit("Hello from bareos!\nLine two.\n");
        bool wok     = os_write_file(path, content);
        printf("write_file: %s\n", wok ? "ok" : "fail");

        Str back = os_read_file(a, path);
        printf("read_file: len=%zu  matches=%d\n",
               back.len,
               str_eq(back, content));

        /* --- 2. os_stat --- */
        FileInfo fi = os_stat(a, path);
        printf("stat: kind=%d (REGULAR=1)  size=%lld  mode=0%o\n",
               fi.kind,
               (long long)fi.size,
               fi.mode);
        printf("stat: mtime_ns > 0: %d\n", fi.mtime_ns > 0);
        printf("stat: uid=%u  gid=%u\n", fi.uid, fi.gid);

        /* --- 3. os_lstat (no symlink follow) --- */
        FileInfo lfi = os_lstat(a, str_lit("/tmp"));
        printf("lstat /tmp: kind=%d (DIRECTORY=2)\n", lfi.kind);

        /* --- 4. Existence predicates --- */
        printf("exists('/tmp')         = %d\n", os_exists(str_lit("/tmp")));
        printf("is_dir('/tmp')         = %d\n", os_is_dir(str_lit("/tmp")));
        printf("is_file(test.txt)      = %d\n", os_is_file(path));
        printf("exists('/no/such/path')= %d\n",
               os_exists(str_lit("/no/such/path")));
        printf("is_dir(test.txt)       = %d\n", os_is_dir(path));

        /* --- 5. os_open and os_close --- */
        File f = os_open(a, path, OS_RDONLY);
        printf("open RDONLY: size=%lld  path=%s\n",
               (long long)f.info.size,
               f.path);
        os_close(&f);
        printf("after close: fd invalid=%d\n", f.fd == OS_INVALID_FD);

        /* --- 6. os_open with write flags --- */
        Str  wpath = str_lit("/tmp/bare_write.txt");
        File wf    = os_open(a, wpath, OS_WRONLY | OS_CREATE | OS_TRUNC);
        printf("open WRONLY|CREATE: ok fd=%d\n", (int)wf.fd);
        os_close(&wf);

        /* --- 7. os_create --- */
        File cf = os_create(a, str_lit("/tmp/bare_created.txt"), 0644);
        printf("create: kind=%d\n", cf.info.kind);
        os_close(&cf);

        /* --- 8. os_mkdir / os_mkdir_all --- */
        Str dir1 = str_lit("/tmp/bare_dir1");
        os_mkdir(dir1, 0755);
        printf("mkdir: exists=%d\n", os_is_dir(dir1));

        Str  deep  = str_lit("/tmp/bare_dir1/a/b/c");
        bool allok = os_mkdir_all(a, deep, 0755);
        printf("mkdir_all: %s  exists=%d\n",
               allok ? "ok" : "fail",
               os_is_dir(deep));

        /* --- 9. os_rename --- */
        Str  path2 = str_lit("/tmp/bare_test2.txt");
        bool rok   = os_rename(path, path2);
        printf("rename: %s  new_exists=%d\n",
               rok ? "ok" : "fail",
               os_exists(path2));

        /* --- 10. os_readdir --- */
        Slice(FileInfo) entries = os_readdir(a, str_lit("/tmp"));
        printf("readdir /tmp: %zu entries\n", slice_len(entries));
        /* Print first few */
        for (size_t i = 0; i < slice_len(entries) && i < 3; i++) {
                printf("  [%zu] \"%s\"  kind=%d\n",
                       i,
                       entries[i].name.ptr,
                       entries[i].kind);
        }

        /* --- 11. FileInfo fields for a directory entry --- */
        if (slice_len(entries) > 0) {
                FileInfo* e = &entries[0];
                printf("entry: size=%lld  mtime_ns=%lld  mode=0%o\n",
                       (long long)e->size,
                       (long long)e->mtime_ns,
                       e->mode);
        }

        /* --- 12. os_getenv --- */
        Str home = os_getenv(a, str_lit("HOME"));
        printf("HOME = \"" StrFmt "\"\n", StrArgs(home));

        Str missing = os_getenv(a, str_lit("BARE_NO_SUCH_VAR_XYZ"));
        printf("missing env is_null=%d\n", str_is_null(missing));

        /* --- 13. os_getcwd --- */
        Str cwd = os_getcwd(a);
        printf("cwd len=%zu  starts-with-/=%d\n",
               cwd.len,
               cwd.len > 0 && cwd.ptr[0] == '/');

        /* --- 14. os_glob --- */
        Slice(Str) globs = os_glob(a, str_lit("/tmp/bare_dir1*"));
        printf("glob '/tmp/bare_dir1*': %zu match(es)\n", slice_len(globs));

        /* --- 15. os_remove --- */
        os_remove(path2);
        os_remove(wpath);
        os_remove(str_lit("/tmp/bare_created.txt"));
        /* Remove nested dirs (must go leaf-first) */
        os_remove(str_lit("/tmp/bare_dir1/a/b/c"));
        os_remove(str_lit("/tmp/bare_dir1/a/b"));
        os_remove(str_lit("/tmp/bare_dir1/a"));
        os_remove(dir1);
        printf("cleanup done, dir1 exists=%d\n", os_exists(dir1));

        arena_free(a);
        printf("done.\n");
        return 0;
}
