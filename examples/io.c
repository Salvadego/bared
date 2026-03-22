#define BARESTD_IMPLEMENTATION
#define BAREIO_IMPLEMENTATION
#include <stdio.h>
#include <string.h>

#include "bareio.h"

int main(void) {
        Arena* a = arena_new(KB(32));

        /* --- 1. FILE* writer (stdout) --- */
        Writer wout = writer_from_file(stdout);
        io_write_str(&wout, str_lit("io_write_str to stdout\n"));

        /* --- 2. FILE* writer raw bytes --- */
        io_write(&wout, "raw ", 4);
        io_write(&wout, "bytes\n", 6);

        /* --- 3. Memory writer: growable arena-backed buffer --- */
        MemWriter mw;
        Writer    w = writer_to_mem(&mw, a, 64);
        io_write_str(&w, str_lit("Hello from MemWriter"));
        io_write(&w, "!", 1);
        Str result = mem_writer_result(&mw);
        printf("MemWriter result: \"" StrFmt "\"\n", StrArgs(result));
        printf("MemWriter len: %zu\n", result.len);

        /* --- 4. Memory reader --- */
        const char data[] = "memory reader content";
        Reader     r      = reader_from_mem(a, data, strlen(data));
        char       buf[32];
        memset(buf, 0, sizeof buf);
        size_t got = io_read(&r, buf, 7);
        printf("mem read %zu bytes: \"%.7s\"\n", got, buf);

        /* --- 5. Partial reads from memory reader --- */
        got = io_read(&r, buf, 7);
        printf("second read %zu bytes: \"%.7s\"\n", got, buf);
        /* read until EOF */
        size_t total = 0;
        while ((got = io_read(&r, buf, sizeof buf)) > 0) total += got;
        printf("remaining bytes: %zu\n", total);

        /* --- 6. io_read_all: read everything into arena --- */
        Reader r2  = reader_from_mem(a, "read all content", 16);
        Str    all = io_read_all(a, &r2);
        printf("read_all: \"" StrFmt "\"\n", StrArgs(all));

        /* --- 7. BufReader: line-by-line reading --- */
        const char lines_data[] = "first line\nsecond line\r\nthird line";
        Reader     lr = reader_from_mem(a, lines_data, strlen(lines_data));
        BufReader* br = bufreader_make(a, lr, 64);
        printf("lines:\n");
        Str line;
        while (!str_is_null(line = buf_read_line(a, br))) {
                printf("  \"" StrFmt "\"\n", StrArgs(line));
        }

        /* --- 8. BufReader: buf_read (binary chunks) --- */
        const char bin_data[] = "abcdefghijklmnopqrstuvwxyz";
        Reader     br_src     = reader_from_mem(a, bin_data, strlen(bin_data));
        BufReader* br2 = bufreader_make(a, br_src, 8); /* 8-byte buffer */
        char       chunk[5];
        size_t     nr = buf_read(br2, chunk, sizeof chunk);
        printf("buf_read chunk: \"%.*s\"\n", (int)nr, chunk);

        /* --- 9. BufWriter with manual flush --- */
        MemWriter  mw2;
        Writer     bwdst = writer_to_mem(&mw2, a, 8);
        BufWriter* bw    = bufwriter_make(a, bwdst, 16);
        buf_write_str(bw, str_lit("buffered "));
        buf_write_str(bw, str_lit("output"));
        /* nothing in dst yet until flush */
        printf("before flush: mw2.len=%zu\n", mw2.len);
        buf_flush(bw);
        printf("after  flush: mw2.len=%zu  \"%s\"\n", mw2.len, (char*)mw2.buf);

        /* --- 10. buf_write with overflow (forces internal flush) --- */
        MemWriter  mw3;
        Writer     bwdst2 = writer_to_mem(&mw3, a, 4);
        BufWriter* bw2    = bufwriter_make(a, bwdst2, 4); /* 4-byte buffer */
        buf_write_str(bw2, str_lit("overflow test string"));
        buf_flush(bw2);
        printf("overflow buf_write: len=%zu\n", mw3.len);

        /* --- 11. io_close: no-op when close_fn is NULL, safe to call --- */
        io_close_reader(&r);
        io_close_writer(&w);
        printf("close with null close_fn: ok\n");

        /* --- 12. io_println / io_print / io_eprint / io_eprintln --- */
        io_print(str_lit("io_print: no newline -"));
        io_println(str_lit(" io_println adds one"));
        io_eprint(str_lit("io_eprint: to stderr\n"));

        arena_free(a);
        printf("done.\n");
        return 0;
}
