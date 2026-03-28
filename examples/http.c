#define BARESTD_IMPLEMENTATION
#define BARETIME_IMPLEMENTATION
#define BARENET_IMPLEMENTATION
#define BARESYNC_IMPLEMENTATION
#define BARETHREADS_IMPLEMENTATION
#define BAREHTTP_IMPLEMENTATION
#define BAREBUILDER_IMPLEMENTATION
#define BAREJSON_IMPLEMENTATION
#include <stdio.h>

#include "barehttp.h"
#include "barejson.h"
#include "baretime.h"

int main(void) {
        Arena* a = arena_new(MB(2));
        net_init();

        /* ---- GET request ---- */
        printf("=== GET http://httpbin.org/get ===\n");
        Instant      t0 = instant_now();
        HttpResponse res =
            http_get(a, str_lit("http://httpbin.org/get?foo=bar&baz=1"));
        double ms = (double)instant_since(t0) / 1e6;

        if (res.error) {
                printf("Error: %s\n", res.error);
        } else {
                printf("Status: %d  (%.0fms)\n", res.status, ms);
                printf("Content-Type: " StrFmt "\n",
                       StrArgs(http_res_header(&res, str_lit("Content-Type"))));

                /* Parse JSON response */
                JsonVal root = json_parse(a, res.body);
                if (!json_is_err(root)) {
                        printf("url:  " StrFmt "\n",
                               StrArgs(json_str(json_get(root, "url"))));
                        /* httpbin echoes back query args */
                        JsonVal args = json_get(root, "args");
                        if (!json_is_null(args)) {
                                printf(
                                    "args.foo: " StrFmt "\n",
                                    StrArgs(json_str(json_get(args, "foo"))));
                                printf(
                                    "args.baz: " StrFmt "\n",
                                    StrArgs(json_str(json_get(args, "baz"))));
                        }
                }
        }

        /* ---- POST request with JSON body ---- */
        printf("\n=== POST http://httpbin.org/post ===\n");

        /* Build JSON body using emitter */
        JsonEmit j = json_emit_begin(a);
        json_obj_start(&j);
        json_key_c(&j, "name");
        json_str_c(&j, "Alice");
        json_key_c(&j, "score");
        json_num_i(&j, 9001);
        json_obj_end(&j);
        char* post_body = json_emit_end(&j);

        HttpClientOpts opts = http_client_opts_default();
        opts.content_type   = str_lit("application/json");

        t0                    = instant_now();
        HttpResponse post_res = http_post(
            a, str_lit("http://httpbin.org/post"), str_from_c(post_body), opts);
        ms = (double)instant_since(t0) / 1e6;

        if (post_res.error) {
                printf("Error: %s\n", post_res.error);
        } else {
                printf("Status: %d  (%.0fms)\n", post_res.status, ms);
                JsonVal root2 = json_parse(a, post_res.body);
                if (!json_is_err(root2)) {
                        /* httpbin echoes back the parsed JSON body */
                        JsonVal data = json_get(root2, "json");
                        if (!json_is_null(data)) {
                                printf(
                                    "echoed name:  " StrFmt "\n",
                                    StrArgs(json_str(json_get(data, "name"))));
                                printf("echoed score: %lld\n",
                                       (long long)json_int(
                                           json_get(data, "score")));
                        }
                }
        }

        /* ---- URL parse test (no network) ---- */
        printf("\n=== URL parsing ===\n");
        /* Just test the URL parser directly */
        struct {
                const char* url;
        } tests[] = {
            {"http://example.com/"},
            {"http://example.com:9090/api/v1?q=test"},
            {"https://api.github.com/users/alice"},
        };
        size_t i;
        for (i = 0; i < sizeof tests / sizeof tests[0]; i++) {
                HttpClientOpts dry = http_client_opts_default();
                dry.timeout_ms = 1; /* very short, will fail but URL parsed */
                HttpResponse r =
                    http_get_opts(a, str_from_c(tests[i].url), dry);
                (void)r; /* just checking compilation / URL parsing, network may
                            fail */
                printf("  %s -> %s\n",
                       tests[i].url,
                       r.error ? r.error : "connected");
        }

        net_cleanup();
        arena_free(a);
        return 0;
}
