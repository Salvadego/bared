/*
 * barejson.h -- JSON parse tree + emitter
 * =========================================
 *
 *  USAGE
 *    #define BAREJSON_IMPLEMENTATION
 *    #include "barejson.h"
 *
 *  DEPENDS ON
 *    barestd.h, barebuilder.h
 *
 *  PARSER
 *  ------
 *  One call builds a value tree in the arena.
 *  All strings point into the source buffer (zero copy, no escapes parsed
 *  unless the string actually contains backslashes).
 *
 *    JsonVal root = json_parse(a, src);
 *    if (json_is_err(root)) { fprintf(stderr, "parse error: %s\n", root.s.ptr);
 * }
 *
 *    Str    name  = json_str(json_get(root, "name"));
 *    double score = json_num(json_get(root, "score"));
 *    bool   ok    = json_bool(json_get(root, "active"));
 *
 *    JsonVal arr = json_get(root, "players");
 *    for (size_t i = 0; i < json_len(arr); i++) {
 *        JsonVal player = json_at(arr, i);
 *        printf(StrFmt ": %lld\n",
 *               StrArgs(json_str(json_get(player, "name"))),
 *               (long long)json_int(json_get(player, "score")));
 *    }
 *
 *  All accessors are safe: wrong type or missing key returns a null/zero
 *  value, never crashes. Deep chaining always works.
 *
 *  EMITTER
 *  -------
 *    JsonEmit j = json_emit_begin(a);
 *    json_obj_start(&j);
 *      json_key(&j, str_lit("name"));  json_str_v(&j, str_lit("Alice"));
 *      json_key(&j, str_lit("score")); json_num_i(&j, 9001);
 *    json_obj_end(&j);
 *    char *out = json_emit_end(&j);   // {"name":"Alice","score":9001}
 *
 *  TYPES
 *    JSON_NULL  JSON_BOOL  JSON_NUM  JSON_STR  JSON_ARR  JSON_OBJ  JSON_ERR
 */
#ifndef BAREJSON_H
#define BAREJSON_H

#include <stdarg.h>

#include "barebuilder.h"
#include "barestd.h"

typedef enum {
        JSON_NULL = 0,
        JSON_BOOL,
        JSON_NUM,
        JSON_STR,
        JSON_ARR,
        JSON_OBJ,
        JSON_ERR,
} JsonType;

typedef struct JsonVal  JsonVal;
typedef struct JsonPair JsonPair;

struct JsonPair {
        Str      key;
        JsonVal* val;
};

struct JsonVal {
        JsonType type;
        union {
                bool   b;
                double n;
                Str    s; /* JSON_STR, JSON_ERR */
                struct {
                        JsonVal** items;
                        size_t    len;
                } arr;
                struct {
                        JsonPair* pairs;
                        size_t    len;
                } obj;
        };
};

/* ================================================================
 *  Parse
 * ================================================================ */
JsonVal json_parse(Arena* a, Str src);

/* ================================================================
 *  Type checks
 * ================================================================ */
static inline bool json_is_null(JsonVal v) {
        return v.type == JSON_NULL;
}
static inline bool json_is_err(JsonVal v) {
        return v.type == JSON_ERR;
}
static inline bool json_is_obj(JsonVal v) {
        return v.type == JSON_OBJ;
}
static inline bool json_is_arr(JsonVal v) {
        return v.type == JSON_ARR;
}
static inline bool json_is_str(JsonVal v) {
        return v.type == JSON_STR;
}
static inline bool json_is_num(JsonVal v) {
        return v.type == JSON_NUM;
}
static inline bool json_is_bool(JsonVal v) {
        return v.type == JSON_BOOL;
}

/* ================================================================
 *  Value extractors (safe — return zero/null on wrong type)
 * ================================================================ */
static inline Str json_str(JsonVal v) {
        return v.type == JSON_STR ? v.s : str_null();
}
static inline double json_num(JsonVal v) {
        return v.type == JSON_NUM ? v.n : 0.0;
}
static inline bool json_bool(JsonVal v) {
        return v.type == JSON_BOOL ? v.b : false;
}
static inline int64_t json_int(JsonVal v) {
        return v.type == JSON_NUM ? (int64_t)v.n : 0;
}
/* NUL-terminated string copy into arena. Returns "" on wrong type. */
static inline const char* json_cstr(Arena* a, JsonVal v) {
        if (v.type != JSON_STR) return "";
        return str_to_cstr(a, v.s);
}
static inline size_t json_len(JsonVal v) {
        if (v.type == JSON_ARR) return v.arr.len;
        if (v.type == JSON_OBJ) return v.obj.len;
        return 0;
}

/* Array index — JSON_NULL if out of bounds */
JsonVal json_at(JsonVal v, size_t idx);

/* Object lookup by C string key — JSON_NULL if missing.
   Safe to chain: json_get(json_get(root,"a"),"b") */
JsonVal json_get(JsonVal v, const char* key);

/* Object lookup by Str key */
JsonVal json_get_str_key(JsonVal v, Str key);

/* Iterate object pairs: json_pair(v, i).key / .val */
static inline JsonPair json_pair_at(JsonVal v, size_t i) {
        JsonPair p;
        p.key = str_null();
        p.val = NULL;
        if (v.type == JSON_OBJ && i < v.obj.len) return v.obj.pairs[i];
        return p;
}

/* ================================================================
 *  Emitter
 * ================================================================ */
#define JSON_EMIT_MAX_DEPTH 64

typedef struct {
        StrBuilder sb;
        int        stack[JSON_EMIT_MAX_DEPTH];
        int        depth;
        bool       needs_comma[JSON_EMIT_MAX_DEPTH];
} JsonEmit;

JsonEmit json_emit_begin(Arena* a);
char*    json_emit_end(JsonEmit* j);
Str      json_emit_view(JsonEmit* j);

void json_obj_start(JsonEmit* j);
void json_obj_end(JsonEmit* j);
void json_arr_start(JsonEmit* j);
void json_arr_end(JsonEmit* j);
void json_key(JsonEmit* j, Str key);
void json_key_c(JsonEmit* j, const char* key);
void json_str_v(JsonEmit* j, Str val); /* emit string value */
void json_str_c(JsonEmit* j, const char* val);
void json_num_d(JsonEmit* j, double val);
void json_num_i(JsonEmit* j, int64_t val);
void json_bool_v(JsonEmit* j, bool val);
void json_null_v(JsonEmit* j);
void json_val(JsonEmit* j, JsonVal v); /* emit an existing tree */

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#ifdef BAREJSON_IMPLEMENTATION
#        include <stdio.h>
#        include <string.h>

typedef struct {
        const char *p, *end;
        Arena*      a;
        const char* err;
} _JP;

static void _jp_ws(_JP* s) {
        while (s->p < s->end && (*s->p == ' ' || *s->p == '\t' ||
                                 *s->p == '\n' || *s->p == '\r'))
                s->p++;
}
static char _jp_peek(_JP* s) {
        _jp_ws(s);
        return s->p < s->end ? *s->p : '\0';
}

static JsonVal _jp_null_val(void) {
        JsonVal v;
        memset(&v, 0, sizeof v);
        return v;
}
static JsonVal _jp_err(_JP* s, const char* msg) {
        s->err = msg;
        JsonVal v;
        v.type = JSON_ERR;
        v.s    = str_from_c(msg);
        return v;
}

static bool _jp_string(_JP* s, Str* out) {
        if (s->p >= s->end || *s->p != '"') {
                s->err = "expected '\"'";
                return false;
        }
        s->p++;
        const char* start   = s->p;
        bool        has_esc = false;
        while (s->p < s->end) {
                char c = *s->p++;
                if (c == '\\') {
                        has_esc = true;
                        if (s->p < s->end) s->p++;
                        continue;
                }
                if (c == '"') {
                        break;
                }
        }
        const char* end_content = s->p - 1;
        size_t      raw_len     = (size_t)(end_content - start);
        if (!has_esc) {
                *out = str_buf(start, raw_len);
                return true;
        }
        char*       buf = arena_push_array(s->a, char, raw_len + 1);
        size_t      o   = 0;
        const char* r   = start;
        while (r < end_content) {
                if (*r != '\\') {
                        buf[o++] = *r++;
                        continue;
                }
                r++;
                if (r >= end_content) break;
                switch (*r++) {
                        case '"':
                                buf[o++] = '"';
                                break;
                        case '\\':
                                buf[o++] = '\\';
                                break;
                        case '/':
                                buf[o++] = '/';
                                break;
                        case 'b':
                                buf[o++] = '\b';
                                break;
                        case 'f':
                                buf[o++] = '\f';
                                break;
                        case 'n':
                                buf[o++] = '\n';
                                break;
                        case 'r':
                                buf[o++] = '\r';
                                break;
                        case 't':
                                buf[o++] = '\t';
                                break;
                        case 'u': {
                                if (r + 4 > end_content) {
                                        buf[o++] = '?';
                                        break;
                                }
                                unsigned int cp = 0;
                                int          k;
                                for (k = 0; k < 4; k++) {
                                        char h = r[k];
                                        int d = (h >= '0' && h <= '9') ? h - '0'
                                                : (h >= 'a' && h <= 'f')
                                                    ? h - 'a' + 10
                                                : (h >= 'A' && h <= 'F')
                                                    ? h - 'A' + 10
                                                    : 0;
                                        cp    = cp * 16 + (unsigned)d;
                                }
                                r += 4;
                                if (cp < 0x80)
                                        buf[o++] = (char)cp;
                                else if (cp < 0x800) {
                                        buf[o++] = (char)(0xC0 | (cp >> 6));
                                        buf[o++] = (char)(0x80 | (cp & 0x3F));
                                } else {
                                        buf[o++] = (char)(0xE0 | (cp >> 12));
                                        buf[o++] =
                                            (char)(0x80 | ((cp >> 6) & 0x3F));
                                        buf[o++] = (char)(0x80 | (cp & 0x3F));
                                }
                                break;
                        }
                        default:
                                buf[o++] = r[-1];
                                break;
                }
        }
        buf[o] = '\0';
        *out   = str_buf(buf, o);
        return true;
}

static JsonVal _jp_val(_JP* s);

static JsonVal _jp_number(_JP* s) {
        _jp_ws(s);
        const char* start = s->p;
        if (s->p < s->end && *s->p == '-') s->p++;
        while (s->p < s->end && *s->p >= '0' && *s->p <= '9') s->p++;
        if (s->p < s->end && *s->p == '.') {
                s->p++;
                while (s->p < s->end && *s->p >= '0' && *s->p <= '9') s->p++;
        }
        if (s->p < s->end && (*s->p == 'e' || *s->p == 'E')) {
                s->p++;
                if (s->p < s->end && (*s->p == '+' || *s->p == '-')) s->p++;
                while (s->p < s->end && *s->p >= '0' && *s->p <= '9') s->p++;
        }
        size_t len = (size_t)(s->p - start);
        char*  tmp = arena_push_array(s->a, char, len + 1);
        memcpy(tmp, start, len);
        tmp[len]  = '\0';
        double v  = 0;
        Str    ts = str_buf(tmp, len);
        str_to_f64(ts, &v);
        JsonVal jv;
        jv.type = JSON_NUM;
        jv.n    = v;
        return jv;
}

static JsonVal _jp_array(_JP* s) {
        s->p++;
        size_t    cap   = 8;
        JsonVal** items = (JsonVal**)arena_push(
            s->a, sizeof(JsonVal*) * cap, sizeof(JsonVal*));
        size_t len = 0;
        _jp_ws(s);
        if (s->p < s->end && *s->p == ']') {
                s->p++;
                goto done;
        }
        for (;;) {
                if (s->err) return _jp_err(s, s->err);
                if (len >= cap) {
                        JsonVal** ni = (JsonVal**)arena_push(
                            s->a, sizeof(JsonVal*) * cap * 2, sizeof(JsonVal*));
                        memcpy(ni, items, sizeof(JsonVal*) * len);
                        items = ni;
                        cap *= 2;
                }
                JsonVal* vp =
                    (JsonVal*)arena_push(s->a, sizeof(JsonVal), sizeof(void*));
                *vp = _jp_val(s);
                if (s->err) return _jp_err(s, s->err);
                items[len++] = vp;
                _jp_ws(s);
                char c = s->p < s->end ? *s->p : '\0';
                if (c == ']') {
                        s->p++;
                        break;
                }
                if (c == ',') {
                        s->p++;
                        continue;
                }
                return _jp_err(s, "expected ',' or ']'");
        }
done: {
        JsonVal v;
        v.type      = JSON_ARR;
        v.arr.items = items;
        v.arr.len   = len;
        return v;
}
}

static JsonVal _jp_object(_JP* s) {
        s->p++;
        size_t    cap = 8;
        JsonPair* pairs =
            (JsonPair*)arena_push(s->a, sizeof(JsonPair) * cap, sizeof(void*));
        size_t len = 0;
        _jp_ws(s);
        if (s->p < s->end && *s->p == '}') {
                s->p++;
                goto done;
        }
        for (;;) {
                if (s->err) return _jp_err(s, s->err);
                if (len >= cap) {
                        JsonPair* np = (JsonPair*)arena_push(
                            s->a, sizeof(JsonPair) * cap * 2, sizeof(void*));
                        memcpy(np, pairs, sizeof(JsonPair) * len);
                        pairs = np;
                        cap *= 2;
                }
                _jp_ws(s);
                if (s->p >= s->end || *s->p != '"')
                        return _jp_err(s, "expected string key");
                Str key;
                if (!_jp_string(s, &key)) return _jp_err(s, s->err);
                _jp_ws(s);
                if (s->p >= s->end || *s->p != ':')
                        return _jp_err(s, "expected ':'");
                s->p++;
                JsonVal* vp =
                    (JsonVal*)arena_push(s->a, sizeof(JsonVal), sizeof(void*));
                *vp = _jp_val(s);
                if (s->err) return _jp_err(s, s->err);
                pairs[len].key = key;
                pairs[len].val = vp;
                len++;
                _jp_ws(s);
                char c = s->p < s->end ? *s->p : '\0';
                if (c == '}') {
                        s->p++;
                        break;
                }
                if (c == ',') {
                        s->p++;
                        continue;
                }
                return _jp_err(s, "expected ',' or '}'");
        }
done: {
        JsonVal v;
        v.type      = JSON_OBJ;
        v.obj.pairs = pairs;
        v.obj.len   = len;
        return v;
}
}

static JsonVal _jp_val(_JP* s) {
        if (s->err) return _jp_err(s, s->err);
        char c = _jp_peek(s);
        switch (c) {
                case '{':
                        return _jp_object(s);
                case '[':
                        return _jp_array(s);
                case '"': {
                        Str str;
                        if (!_jp_string(s, &str)) return _jp_err(s, s->err);
                        JsonVal v;
                        v.type = JSON_STR;
                        v.s    = str;
                        return v;
                }
                case 't':
                        if (s->p + 4 <= s->end &&
                            memcmp(s->p, "true", 4) == 0) {
                                s->p += 4;
                                JsonVal v;
                                v.type = JSON_BOOL;
                                v.b    = true;
                                return v;
                        }
                        return _jp_err(s, "bad token");
                case 'f':
                        if (s->p + 5 <= s->end &&
                            memcmp(s->p, "false", 5) == 0) {
                                s->p += 5;
                                JsonVal v;
                                v.type = JSON_BOOL;
                                v.b    = false;
                                return v;
                        }
                        return _jp_err(s, "bad token");
                case 'n':
                        if (s->p + 4 <= s->end &&
                            memcmp(s->p, "null", 4) == 0) {
                                s->p += 4;
                                return _jp_null_val();
                        }
                        return _jp_err(s, "bad token");
                case '\0':
                        return _jp_err(s, "unexpected end");
                default:
                        if (c == '-' || (c >= '0' && c <= '9'))
                                return _jp_number(s);
                        return _jp_err(s, "unexpected character");
        }
}

JsonVal json_parse(Arena* a, Str src) {
        _JP s;
        s.p       = src.ptr;
        s.end     = src.ptr + src.len;
        s.a       = a;
        s.err     = NULL;
        JsonVal v = _jp_val(&s);
        if (s.err) {
                JsonVal e;
                e.type = JSON_ERR;
                e.s    = str_from_c(s.err);
                return e;
        }
        return v;
}

JsonVal json_at(JsonVal v, size_t idx) {
        if (v.type != JSON_ARR || idx >= v.arr.len) return _jp_null_val();
        return *v.arr.items[idx];
}
JsonVal json_get(JsonVal v, const char* key) {
        return json_get_str_key(v, str_from_c(key));
}
JsonVal json_get_str_key(JsonVal v, Str key) {
        if (v.type != JSON_OBJ) return _jp_null_val();
        size_t i;
        for (i = 0; i < v.obj.len; i++)
                if (str_eq(v.obj.pairs[i].key, key)) return *v.obj.pairs[i].val;
        return _jp_null_val();
}

/* ---- Emitter ---- */
static void _je_comma(JsonEmit* j) {
        if (j->depth > 0 && j->needs_comma[j->depth - 1])
                sb_write_char(&j->sb, ',');
        if (j->depth > 0) j->needs_comma[j->depth - 1] = true;
}
static void _je_esc(JsonEmit* j, Str s) {
        sb_write_char(&j->sb, '"');
        size_t i;
        for (i = 0; i < s.len; i++) {
                unsigned char c = (unsigned char)s.ptr[i];
                if (c == '"')
                        sb_write_cstr(&j->sb, "\\\"");
                else if (c == '\\')
                        sb_write_cstr(&j->sb, "\\\\");
                else if (c == '\n')
                        sb_write_cstr(&j->sb, "\\n");
                else if (c == '\r')
                        sb_write_cstr(&j->sb, "\\r");
                else if (c == '\t')
                        sb_write_cstr(&j->sb, "\\t");
                else if (c < 0x20) {
                        char esc[8];
                        snprintf(esc, sizeof esc, "\\u%04X", c);
                        sb_write_cstr(&j->sb, esc);
                } else
                        sb_write_char(&j->sb, (char)c);
        }
        sb_write_char(&j->sb, '"');
}
JsonEmit json_emit_begin(Arena* a) {
        JsonEmit j;
        memset(&j, 0, sizeof j);
        j.sb = sb_make(a, 256);
        return j;
}
char* json_emit_end(JsonEmit* j) {
        return sb_to_cstr(j->sb.mark.a, &j->sb);
}
Str json_emit_view(JsonEmit* j) {
        return sb_to_str(&j->sb);
}
void json_obj_start(JsonEmit* j) {
        _je_comma(j);
        sb_write_char(&j->sb, '{');
        if (j->depth < JSON_EMIT_MAX_DEPTH) {
                j->stack[j->depth]       = 1;
                j->needs_comma[j->depth] = false;
                j->depth++;
        }
}
void json_obj_end(JsonEmit* j) {
        if (j->depth > 0) j->depth--;
        sb_write_char(&j->sb, '}');
        if (j->depth > 0) j->needs_comma[j->depth - 1] = true;
}
void json_arr_start(JsonEmit* j) {
        _je_comma(j);
        sb_write_char(&j->sb, '[');
        if (j->depth < JSON_EMIT_MAX_DEPTH) {
                j->stack[j->depth]       = 0;
                j->needs_comma[j->depth] = false;
                j->depth++;
        }
}
void json_arr_end(JsonEmit* j) {
        if (j->depth > 0) j->depth--;
        sb_write_char(&j->sb, ']');
        if (j->depth > 0) j->needs_comma[j->depth - 1] = true;
}
void json_key(JsonEmit* j, Str key) {
        if (j->depth > 0 && j->needs_comma[j->depth - 1])
                sb_write_char(&j->sb, ',');
        if (j->depth > 0) j->needs_comma[j->depth - 1] = false;
        _je_esc(j, key);
        sb_write_char(&j->sb, ':');
        /* do NOT set needs_comma here — the following value call will do it */
}
void json_key_c(JsonEmit* j, const char* k) {
        json_key(j, str_from_c(k));
}
void json_str_v(JsonEmit* j, Str v) {
        _je_comma(j);
        _je_esc(j, v);
}
void json_str_c(JsonEmit* j, const char* v) {
        json_str_v(j, str_from_c(v));
}
void json_num_d(JsonEmit* j, double v) {
        _je_comma(j);
        char buf[64];
        if (v == (double)(int64_t)v && v >= -1e15 && v <= 1e15)
                snprintf(buf, sizeof buf, "%lld", (long long)(int64_t)v);
        else
                snprintf(buf, sizeof buf, "%g", v);
        sb_write_cstr(&j->sb, buf);
}
void json_num_i(JsonEmit* j, int64_t v) {
        _je_comma(j);
        char buf[32];
        snprintf(buf, sizeof buf, "%lld", (long long)v);
        sb_write_cstr(&j->sb, buf);
}
void json_bool_v(JsonEmit* j, bool v) {
        _je_comma(j);
        sb_write_cstr(&j->sb, v ? "true" : "false");
}
void json_null_v(JsonEmit* j) {
        _je_comma(j);
        sb_write_cstr(&j->sb, "null");
}
void json_val(JsonEmit* j, JsonVal v) {
        switch (v.type) {
                case JSON_NULL:
                        json_null_v(j);
                        break;
                case JSON_BOOL:
                        json_bool_v(j, v.b);
                        break;
                case JSON_NUM:
                        json_num_d(j, v.n);
                        break;
                case JSON_STR:
                        json_str_v(j, v.s);
                        break;
                case JSON_ARR: {
                        json_arr_start(j);
                        size_t i;
                        for (i = 0; i < v.arr.len; i++)
                                json_val(j, *v.arr.items[i]);
                        json_arr_end(j);
                        break;
                }
                case JSON_OBJ: {
                        json_obj_start(j);
                        size_t i;
                        for (i = 0; i < v.obj.len; i++) {
                                json_key(j, v.obj.pairs[i].key);
                                json_val(j, *v.obj.pairs[i].val);
                        }
                        json_obj_end(j);
                        break;
                }
                default:
                        json_null_v(j);
                        break;
        }
}
#endif /* BAREJSON_IMPLEMENTATION */
#endif /* BAREJSON_H */
