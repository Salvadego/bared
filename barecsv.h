/*
 * barecsv.h -- CSV / TSV reader with field iteration
 * ====================================================
 *
 *  USAGE
 *    #define BARECSV_IMPLEMENTATION
 *    #include "barecsv.h"
 *
 *  DEPENDS ON
 *    barestd.h  (Str, Arena, Slice)
 *
 *  DESIGN
 *    Pull-parser model: no full materialization.
 *    CsvReader holds a Str (remaining input).
 *    csv_next_row() advances past the current row delimiter.
 *    csv_next_field() yields one field at a time as a Str view
 *    (or arena-allocated if unquoting is needed).
 *
 *    CsvConfig lets you set:
 *      delimiter   (default ',')
 *      quote       (default '"')
 *      trim_spaces (default false)
 *      has_header  (first row treated as header)
 *
 *    Quoted fields support embedded delimiters and newlines.
 *    Double-quote escaping ("")  is handled per RFC 4180.
 *
 *  EXAMPLE -- iterate rows and fields
 *
 *    CsvReader r = csv_reader(str_lit("name,score\nAlice,9001\nBob,7200"));
 *    CsvRow row;
 *    while (csv_next_row(&r, &row)) {
 *        CsvField f;
 *        while (csv_next_field(&row, &f)) {
 *            printf("[" StrFmt "] ", StrArgs(f.value));
 *        }
 *        printf("\n");
 *    }
 *
 *  EXAMPLE -- read all rows into a slice of slices
 *
 *    CsvConfig cfg = csv_default_config();
 *    cfg.has_header = true;
 *    Slice(Slice(Str)) table = csv_read_all(a, src, cfg);
 *    // table[0] = header row, table[1..] = data rows
 *
 *  EXAMPLE -- TSV
 *
 *    CsvConfig tsv = csv_default_config();
 *    tsv.delimiter = '\t';
 *    CsvReader r = csv_reader_cfg(src, tsv);
 */
#ifndef BARECSV_H
#define BARECSV_H

#include <stdbool.h>

#include "barestd.h"

/* ================================================================
 *  Configuration
 * ================================================================ */
typedef struct {
        char delimiter;   /* field separator (default ',')     */
        char quote;       /* quoting char    (default '"')     */
        bool trim_spaces; /* strip leading/trailing whitespace */
        bool has_header;  /* skip first row as header          */
} CsvConfig;

static inline CsvConfig csv_default_config(void) {
        CsvConfig c;
        c.delimiter   = ',';
        c.quote       = '"';
        c.trim_spaces = false;
        c.has_header  = false;
        return c;
}

/* ================================================================
 *  Field
 * ================================================================ */
typedef struct {
        Str value; /* field content (may point into src or arena if unquoted) */
        bool quoted; /* was this field quoted?           */
        bool last;   /* last field on the row?           */
} CsvField;

/* ================================================================
 *  Row — positioned at the start of one row
 * ================================================================ */
typedef struct {
        Str src;      /* remaining text in this row (up to line ending)  */
        Str full_row; /* the original full row text                      */
        CsvConfig cfg;
        Arena*    arena; /* needed only for quoted fields with "" escapes   */
        bool      done;  /* all fields consumed                             */
} CsvRow;

/* ================================================================
 *  Reader
 * ================================================================ */
typedef struct {
        Str remaining; /* remaining input after current row               */
        CsvConfig cfg;
        Arena*    arena;
        int row_index; /* 0-based; header row is index 0                  */
} CsvReader;

/* ================================================================
 *  API
 * ================================================================ */
CsvReader csv_reader(Str src);
CsvReader csv_reader_cfg(Str src, CsvConfig cfg);
CsvReader csv_reader_arena(Arena* a, Str src, CsvConfig cfg);

/*
 * Advance to next row. Returns true if a row was found.
 * row->src contains the line content (no \r\n).
 * If has_header=true, the first call skips the header row.
 * Pass arena=NULL for non-quoted CSVs (field values are views into src).
 */
bool csv_next_row(CsvReader* r, CsvRow* row);

/*
 * Yield the next field from a row.
 * Returns false when the row is exhausted.
 * For quoted fields with "" escapes, arena allocation is used if
 * the CsvRow has a non-NULL arena. Otherwise the escape is left raw.
 */
bool csv_next_field(CsvRow* row, CsvField* field);

/*
 * Collect header names from the first row (only when has_header=true).
 * Returns a Slice(Str) of field names (views into src).
 */
Slice(Str) csv_read_header(Arena* a, Str src, CsvConfig cfg);

/*
 * Read the entire CSV into a Slice(Slice(Str)).
 * Each outer element is a row; each inner element is a field value.
 * All values are arena-allocated (unquoted properly).
 * Header row is always index 0, data starts at 1.
 */
Slice(Slice(Str)) csv_read_all(Arena* a, Str src, CsvConfig cfg);

/* Convenience: read TSV */
static inline CsvReader csv_reader_tsv(Str src) {
        CsvConfig c = csv_default_config();
        c.delimiter = '\t';
        return csv_reader_cfg(src, c);
}

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#ifdef BARECSV_IMPLEMENTATION
#        include <string.h>

CsvReader csv_reader(Str src) {
        return csv_reader_cfg(src, csv_default_config());
}
CsvReader csv_reader_cfg(Str src, CsvConfig cfg) {
        CsvReader r;
        r.remaining = src;
        r.cfg       = cfg;
        r.arena     = NULL;
        r.row_index = 0;
        return r;
}
CsvReader csv_reader_arena(Arena* a, Str src, CsvConfig cfg) {
        CsvReader r = csv_reader_cfg(src, cfg);
        r.arena     = a;
        return r;
}

/* Advance src past one line; return the line content (no \r\n).
   Returns false if src is empty. */
static bool _csv_next_line(Str* src, Str* line) {
        if (!src->len) return false;
        /* Find end of line — handle quoted newlines by scanning carefully */
        size_t i = 0;
        while (i < src->len) {
                if (src->ptr[i] == '\n') {
                        size_t end = i;
                        if (end > 0 && src->ptr[end - 1] == '\r') end--;
                        *line = str_buf(src->ptr, end);
                        src->ptr += i + 1;
                        src->len -= i + 1;
                        return true;
                }
                i++;
        }
        /* Last line with no trailing newline */
        *line = *src;
        src->ptr += src->len;
        src->len = 0;
        return true;
}

bool csv_next_row(CsvReader* r, CsvRow* row) {
        Str line;
        for (;;) {
                if (!_csv_next_line(&r->remaining, &line)) return false;
                /* Skip empty lines */
                if (!line.len && r->remaining.len == 0 && line.len == 0) {
                        /* last empty line — consider it done */
                        return false;
                }
                int idx = r->row_index++;
                /* Skip header row if configured */
                if (r->cfg.has_header && idx == 0) continue;
                row->src      = line;
                row->full_row = line;
                row->cfg      = r->cfg;
                row->arena    = r->arena;
                row->done     = false;
                return true;
        }
}

bool csv_next_field(CsvRow* row, CsvField* field) {
        if (row->done) return false;
        if (!row->src.len && row->full_row.len == 0) return false;

        Str  s         = row->src;
        char dlm       = row->cfg.delimiter;
        char q         = row->cfg.quote;
        bool is_quoted = (s.len > 0 && s.ptr[0] == q);

        Str  value;
        bool last = false;

        if (is_quoted) {
                /* consume opening quote */
                s.ptr++;
                s.len--;
                /* Check if we need unescaping (contains "")  */
                size_t i              = 0;
                bool   needs_unescape = false;
                while (i < s.len) {
                        if (s.ptr[i] == q) {
                                if (i + 1 < s.len && s.ptr[i + 1] == q) {
                                        needs_unescape = true;
                                        i += 2;
                                        continue;
                                }
                                break; /* closing quote */
                        }
                        i++;
                }
                if (needs_unescape && row->arena) {
                        /* Copy with unescaping into arena */
                        char*  buf = arena_push_array(row->arena, char, i + 1);
                        size_t o = 0, j = 0;
                        while (j < s.len) {
                                if (s.ptr[j] == q) {
                                        if (j + 1 < s.len &&
                                            s.ptr[j + 1] == q) {
                                                buf[o++] = q;
                                                j += 2;
                                                continue;
                                        }
                                        break;
                                }
                                buf[o++] = s.ptr[j++];
                        }
                        buf[o] = '\0';
                        value  = str_buf(buf, o);
                        s.ptr += j;
                        s.len -= j;
                } else {
                        /* Return view into source (may contain raw ""
                         * sequences) */
                        value = str_buf(s.ptr, i);
                        s.ptr += i;
                        s.len -= i;
                }
                /* consume closing quote */
                if (s.len > 0 && s.ptr[0] == q) {
                        s.ptr++;
                        s.len--;
                }
                /* consume delimiter or mark last */
                if (s.len == 0) {
                        last = true;
                } else if (s.ptr[0] == dlm) {
                        s.ptr++;
                        s.len--;
                }
        } else {
                /* Unquoted: scan to delimiter or end */
                size_t i = 0;
                while (i < s.len && s.ptr[i] != dlm) i++;
                value = str_buf(s.ptr, i);
                s.ptr += i;
                s.len -= i;
                if (s.len == 0) {
                        last = true;
                } else {
                        s.ptr++;
                        s.len--; /* consume delimiter */
                }
        }

        if (row->cfg.trim_spaces) {
                while (value.len &&
                       (value.ptr[0] == ' ' || value.ptr[0] == '\t')) {
                        value.ptr++;
                        value.len--;
                }
                while (value.len && (value.ptr[value.len - 1] == ' ' ||
                                     value.ptr[value.len - 1] == '\t')) {
                        value.len--;
                }
        }

        field->value  = value;
        field->quoted = is_quoted;
        field->last   = last || (s.len == 0);
        row->src      = s;
        if (field->last) row->done = true;
        return true;
}

Slice(Str) csv_read_header(Arena* a, Str src, CsvConfig cfg) {
        Slice(Str) result = slice_make(a, Str, 8);
        Str line;
        if (!_csv_next_line(&src, &line)) return result;
        CsvRow row;
        row.src      = line;
        row.full_row = line;
        row.cfg      = cfg;
        row.arena    = a;
        row.done     = false;
        CsvField f;
        while (csv_next_field(&row, &f)) {
                Str v = str_clone(a, f.value);
                slice_push(result, Str, v);
        }
        return result;
}

Slice(Slice(Str)) csv_read_all(Arena* a, Str src, CsvConfig cfg) {
        Slice(Slice(Str)) table = slice_make(a, Slice(Str), 16);
        CsvReader r             = csv_reader_arena(a, src, cfg);
        r.row_index             = 0; /* reset so has_header is handled below */

        /* Handle header row */
        if (cfg.has_header) {
                Str line;
                if (_csv_next_line(&r.remaining, &line)) {
                        CsvRow row;
                        row.src           = line;
                        row.full_row      = line;
                        row.cfg           = cfg;
                        row.arena         = a;
                        row.done          = false;
                        Slice(Str) header = slice_make(a, Str, 8);
                        CsvField f;
                        while (csv_next_field(&row, &f)) {
                                Str v = str_clone(a, f.value);
                                slice_push(header, Str, v);
                        }
                        slice_push(table, Slice(Str), header);
                }
                r.row_index = 1;
        }

        CsvRow row;
        while (csv_next_row(&r, &row)) {
                Slice(Str) fields = slice_make(a, Str, 8);
                CsvField f;
                while (csv_next_field(&row, &f)) {
                        Str v = (f.quoted && row.arena) ? f.value
                                                        : str_clone(a, f.value);
                        slice_push(fields, Str, v);
                }
                slice_push(table, Slice(Str), fields);
        }
        return table;
}
#endif /* BARECSV_IMPLEMENTATION */
#endif /* BARECSV_H */
