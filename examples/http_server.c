/*
 * examples/http_server.c
 * =======================
 * A real REST API server built with barehttp.h + barejson.h
 *
 * Scenario: game leaderboard API
 *
 *   GET  /                        → welcome HTML page
 *   GET  /health                  → {"status":"ok","uptime_ms":N}
 *   GET  /players                 → JSON array of all active players
 *   POST /players                 → body: {"name":"Alice"} → create player
 *   GET  /players/:id             → single player JSON
 *   DELETE /players/:id           → deactivate player
 *   POST /players/:id/score       → body: {"delta":N} → add to score
 *   GET  /leaderboard             → top-10 sorted by score
 *   GET  /leaderboard?limit=N     → top-N
 *
 * Demonstrates:
 *   - Named path parameters:  /players/:id
 *   - barejson for parsing request bodies AND building responses
 *   - http_json / http_text / http_html response helpers
 *   - Method-specific routing (GET-only, POST-only)
 *   - Thread-pool server (http_serve_pool)
 *   - Shared state with Mutex via ctx->user
 *   - Arena-per-request (no malloc in handlers)
 *   - Structured error responses
 *
 * Test (server must be running):
 *   curl http://localhost:8080/
 *   curl http://localhost:8080/health
 *   curl http://localhost:8080/players
 *   curl -X POST http://localhost:8080/players \
 *        -H "Content-Type: application/json" \
 *        -d '{"name":"Alice"}'
 *   curl http://localhost:8080/players/1
 *   curl -X POST http://localhost:8080/players/1/score \
 *        -d '{"delta":500}'
 *   curl http://localhost:8080/leaderboard
 *   curl 'http://localhost:8080/leaderboard?limit=3'
 */
#define BARESTD_IMPLEMENTATION
#define BARETIME_IMPLEMENTATION
#define BARENET_IMPLEMENTATION
#define BARESYNC_IMPLEMENTATION
#define BARETHREADS_IMPLEMENTATION
#define BAREHTTP_IMPLEMENTATION
#define BAREBUILDER_IMPLEMENTATION
#define BAREJSON_IMPLEMENTATION
#include "barehttp.h"
#include "barejson.h"
#include "baretime.h"

/* ================================================================
 *  Data model
 * ================================================================ */
#define MAX_PLAYERS 64

typedef struct {
        uint32_t id;
        char     name[32];
        int64_t  score;
        uint64_t games;
        bool     active;
} Player;

typedef struct {
        Player   players[MAX_PLAYERS];
        size_t   count;
        uint32_t next_id;
        Mutex    mu;
        Instant  start_time;
} App;

static void emit_player(JsonEmit* j, const Player* p) {
        json_obj_start(j);
        {
                json_key_c(j, "id");     json_num_i(j,  p->id);
                json_key_c(j, "name");   json_str_c(j,  p->name);
                json_key_c(j, "score");  json_num_i(j,  p->score);
                json_key_c(j, "games");  json_num_i(j,  (int64_t)p->games);
                json_key_c(j, "active"); json_bool_v(j, p->active);
        }
        json_obj_end(j);
}

static void emit_error(JsonEmit* j, int code, const char* msg) {
        json_obj_start(j);
        {
                json_key_c(j, "error"); json_str_c(j, msg);
                json_key_c(j, "code");  json_num_i(j, code);
        }
        json_obj_end(j);
}

static void json_err(HttpResponseWriter* w,
                     Arena*              a,
                     int                 status,
                     const char*         msg) {
        JsonEmit j = json_emit_begin(a);
        emit_error(&j, status, msg);
        http_json(w, status, str_from_c(json_emit_end(&j)));
}

/* ================================================================
 *  GET /
 * ================================================================ */
static void handle_root(HttpResponseWriter* w, HttpRequest* req, HttpCtx* ctx) {
        (void)req;
        http_html(w,
                  HTTP_200,
                  str_lit("<!DOCTYPE html><html><body>"
                          "<h1>Leaderboard API</h1>"
                          "<ul>"
                          "<li><a href='/health'>GET /health</a></li>"
                          "<li><a href='/players'>GET /players</a></li>"
                          "<li><a href='/leaderboard'>GET /leaderboard</a></li>"
                          "</ul>"
                          "</body></html>"));
        (void)ctx;
}

/* ================================================================
 *  GET /health
 * ================================================================ */
static void handle_health(HttpResponseWriter* w,
                          HttpRequest*        req,
                          HttpCtx*            ctx) {
        (void)req;
        App*     app    = (App*)ctx->user;
        int64_t  uptime = (int64_t)(instant_since(app->start_time) / 1000000LL);
        JsonEmit j      = json_emit_begin(ctx->arena);

        json_obj_start(&j);
        {
                json_key_c(&j, "status");    json_str_c(&j, "ok");
                json_key_c(&j, "uptime_ms"); json_num_i(&j, uptime);
                json_key_c(&j, "players");   json_num_i(&j, (int64_t)app->count);
        }
        json_obj_end(&j);

        http_json(w, HTTP_200, str_from_c(json_emit_end(&j)));
}

/* ================================================================
 *  GET /players  — list all active players
 * ================================================================ */
static void handle_list_players(HttpResponseWriter* w,
                                HttpRequest*        req,
                                HttpCtx*            ctx) {
        (void)req;
        App*     app = (App*)ctx->user;
        JsonEmit j   = json_emit_begin(ctx->arena);

        json_arr_start(&j);
        {
                mutex_lock(&app->mu);
                size_t i;
                for (i = 0; i < app->count; i++) {
                        if (app->players[i].active) {
                                emit_player(&j, &app->players[i]);
                        }
                }
                mutex_unlock(&app->mu);
        }
        json_arr_end(&j);

        http_json(w, HTTP_200, str_from_c(json_emit_end(&j)));
}

/* ================================================================
 *  POST /players  — create a player
 *  body: {"name":"Alice"}
 * ================================================================ */
static void handle_create_player(HttpResponseWriter* w,
                                 HttpRequest*        req,
                                 HttpCtx*            ctx) {
        App* app = (App*)ctx->user;

        /* Parse body with barejson */
        JsonVal body = json_parse(ctx->arena, req->body);
        if (json_is_err(body)) {
                json_err(w, ctx->arena, HTTP_400, "invalid JSON body");
                return;
        }
        Str name = json_str(json_get(body, "name"));
        if (str_is_null(name) || name.len == 0) {
                json_err(w, ctx->arena, HTTP_400, "missing field: name");
                return;
        }
        if (name.len > 31) {
                json_err(w, ctx->arena, HTTP_400, "name too long");
                return;
        }

        mutex_lock(&app->mu);
        Player *p = NULL;
        {
                if (app->count >= MAX_PLAYERS) {
                        mutex_unlock(&app->mu);
                        json_err(w, ctx->arena, 503, "server full");
                        return;
                }

                p = &app->players[app->count++];
                memset(p, 0, sizeof *p);
                p->id     = ++app->next_id;
                p->active = true;
                memcpy(p->name, name.ptr, name.len);
        }
        mutex_unlock(&app->mu);

        JsonEmit j = json_emit_begin(ctx->arena);
        emit_player(&j, p);
        http_json(w, HTTP_201, str_from_c(json_emit_end(&j)));
}

/* ================================================================
 *  GET /players/:id  — single player
 * ================================================================ */
static void handle_get_player(HttpResponseWriter* w,
                              HttpRequest*        req,
                              HttpCtx*            ctx) {
        App*    app    = (App*)ctx->user;
        Str     id_str = http_path_seg(req, 1);
        int64_t id     = 0;
        str_to_i64(id_str, &id);

        mutex_lock(&app->mu);
        Player* found = NULL;
        {
                size_t  i;
                for (i = 0; i < app->count; i++)
                        if ((int64_t)app->players[i].id == id) {
                                found = &app->players[i];
                                break;
                        }
        }
        mutex_unlock(&app->mu);

        if (!found) {
                json_err(w, ctx->arena, HTTP_404, "player not found");
                return;
        }

        JsonEmit j = json_emit_begin(ctx->arena);
        emit_player(&j, found);
        http_json(w, HTTP_200, str_from_c(json_emit_end(&j)));
}

/* ================================================================
 *  DELETE /players/:id  — deactivate
 * ================================================================ */
static void handle_delete_player(HttpResponseWriter* w,
                                 HttpRequest*        req,
                                 HttpCtx*            ctx) {
        App*    app    = (App*)ctx->user;
        Str     id_str = http_path_seg(req, 1);
        int64_t id     = 0;
        str_to_i64(id_str, &id);

        mutex_lock(&app->mu);
        Player* found = NULL;
        {
                size_t  i;
                for (i = 0; i < app->count; i++)
                        if ((int64_t)app->players[i].id == id) {
                                found = &app->players[i];
                                break;
                        }
                if (found) found->active = false;
        }
        mutex_unlock(&app->mu);

        if (!found) {
                json_err(w, ctx->arena, HTTP_404, "player not found");
                return;
        }
        http_json(w, HTTP_200, str_lit("{\"deleted\":true}"));
}

/* ================================================================
 *  POST /players/:id/score  — add to player's score
 *  body: {"delta": N}
 * ================================================================ */
static void handle_add_score(HttpResponseWriter* w,
                             HttpRequest*        req,
                             HttpCtx*            ctx) {
        App*    app    = (App*)ctx->user;
        Str     id_str = http_path_seg(req, 1);
        int64_t id     = 0;
        str_to_i64(id_str, &id);

        JsonVal body = json_parse(ctx->arena, req->body);
        if (json_is_err(body)) {
                json_err(w, ctx->arena, HTTP_400, "invalid JSON body");
                return;
        }
        int64_t delta = json_int(json_get(body, "delta"));

        mutex_lock(&app->mu);
        Player* found = NULL;
        {
                size_t  i;
                for (i = 0; i < app->count; i++)
                        if ((int64_t)app->players[i].id == id &&
                                        app->players[i].active) {
                                found = &app->players[i];
                                break;
                        }
                if (found) {
                        found->score += delta;
                        found->games++;
                }
        }
        mutex_unlock(&app->mu);

        if (!found) {
                json_err(w, ctx->arena, HTTP_404, "player not found");
                return;
        }

        JsonEmit j = json_emit_begin(ctx->arena);
        emit_player(&j, found);
        http_json(w, HTTP_200, str_from_c(json_emit_end(&j)));
}

/* ================================================================
 *  GET /leaderboard[?limit=N]
 * ================================================================ */
static void handle_leaderboard(HttpResponseWriter* w,
                               HttpRequest*        req,
                               HttpCtx*            ctx) {
        App* app = (App*)ctx->user;

        Str     limit_str = http_query(req, str_lit("limit"));
        int64_t limit     = 10;
        if (!str_is_null(limit_str)) str_to_i64(limit_str, &limit);
        if (limit <= 0 || limit > MAX_PLAYERS) limit = 10;

        /* Copy + sort active players by score (descending) */
        Player sorted[MAX_PLAYERS];
        size_t n = 0;
        mutex_lock(&app->mu);
        size_t i;
        for (i = 0; i < app->count; i++)
                if (app->players[i].active) sorted[n++] = app->players[i];
        mutex_unlock(&app->mu);

        /* Bubble sort (n is small) */
        size_t a, b;
        for (a = 0; a < n; a++)
                for (b = a + 1; b < n; b++)
                        if (sorted[b].score > sorted[a].score) {
                                Player tmp = sorted[a];
                                sorted[a]  = sorted[b];
                                sorted[b]  = tmp;
                        }

        if ((size_t)limit > n) limit = (int64_t)n;

        JsonEmit j = json_emit_begin(ctx->arena);
        json_obj_start(&j);
        {
                json_key_c(&j, "total"); json_num_i(&j, (int64_t)n);
                json_key_c(&j, "limit"); json_num_i(&j, limit);
                json_key_c(&j, "ranks");
                json_arr_start(&j);
                {
                        for (i = 0; i < (size_t)limit; i++) {
                                json_obj_start(&j);
                                {
                                        json_key_c(&j, "rank"); json_num_i(&j, (int64_t)(i + 1));
                                        json_key_c(&j, "id"); json_num_i(&j, sorted[i].id);
                                        json_key_c(&j, "name"); json_str_c(&j, sorted[i].name);
                                        json_key_c(&j, "score"); json_num_i(&j, sorted[i].score);
                                }
                                json_obj_end(&j);
                        }
                }
                json_arr_end(&j);
        }
        json_obj_end(&j);

        http_json(w, HTTP_200, str_from_c(json_emit_end(&j)));
}

/* ================================================================
 *  Dispatcher for /players/:id and /players/:id/score
 *  (needed because we have one prefix /players/ for multiple methods)
 * ================================================================ */
static void handle_player_dispatch(HttpResponseWriter* w,
                HttpRequest*        req,
                HttpCtx*            ctx) {
        /* /players/:id          GET -> get, DELETE -> delete
           /players/:id/score    POST -> add score            */
        Str seg2 = http_path_seg(req, 2);
        if (!str_is_null(seg2) && str_eq(seg2, str_lit("score"))) {
                /* POST /players/:id/score */
                if (!str_eq(req->method, str_lit("POST"))) {
                        http_method_not_allowed(w);
                        return;
                }
                handle_add_score(w, req, ctx);
                return;
        }
        /* /players/:id */
        if (str_eq(req->method, str_lit("GET"))) {
                handle_get_player(w, req, ctx);
        } else if (str_eq(req->method, str_lit("DELETE"))) {
                handle_delete_player(w, req, ctx);
        } else {
                http_method_not_allowed(w);
        }
}

/* ================================================================
 *  main
 * ================================================================ */
int main(void) {
        Arena* a = arena_new(MB(4));

        App* app = arena_push_type(a, App);
        memset(app, 0, sizeof *app);
        app->start_time = instant_now();
        app->next_id    = 0;

        /* Seed some players */
        {
                const char* names[]  = {"Alice", "Bob", "Carol", "Dave", "Eve"};
                int64_t     scores[] = {9001, 7200, 8850, 3100, 9999};
                size_t      k;
                for (k = 0; k < 5; k++) {
                        Player* p = &app->players[k];
                        p->id     = ++app->next_id;
                        p->score  = scores[k];
                        p->games  = (uint64_t)(20 + k * 5);
                        p->active = true;
                        strncpy(p->name, names[k], 31);
                }
                app->count = 5;
        }

        HttpMux* mux = http_mux_new(a);

        http_handle_ctx(mux, str_lit("/"), handle_root, app);
        http_handle_ctx(mux, str_lit("/health"), handle_health, app);
        http_handle_ctx_method(mux, str_lit("/players"), str_lit("GET"), handle_list_players, app);
        http_handle_ctx_method(mux,
                               str_lit("/players"),
                               str_lit("POST"),
                               handle_create_player,
                               app);
        /* /players/ prefix catches /players/:id and /players/:id/score */
        http_handle_ctx(mux, str_lit("/players/"), handle_player_dispatch, app);
        http_handle_ctx(mux, str_lit("/leaderboard"), handle_leaderboard, app);

        uint16_t port = 8080;
        printf("Leaderboard API listening on http://localhost:%u\n", port);
        printf("  GET  /\n");
        printf("  GET  /health\n");
        printf("  GET  /players\n");
        printf("  POST /players          body: {\"name\":\"Alice\"}\n");
        printf("  GET  /players/:id\n");
        printf("  DELETE /players/:id\n");
        printf("  POST /players/:id/score  body: {\"delta\":N}\n");
        printf("  GET  /leaderboard[?limit=N]\n");
        fflush(stdout);

        return http_serve_pool(mux, str_lit("0.0.0.0"), port, 4, a);
}
