#define BARETIME_IMPLEMENTATION
#define BARESTD_IMPLEMENTATION
#include <stdio.h>

#include "baretime.h"

int main(void) {
        Arena* a = arena_new(KB(8));

        /* --- 1. Duration constants --- */
        printf("1 NS  = %lld\n", (long long)NS);
        printf("1 US  = %lld\n", (long long)US);
        printf("1 MS  = %lld\n", (long long)MS);
        printf("1 SEC = %lld\n", (long long)SEC);
        printf("1 MIN = %lld\n", (long long)MIN);
        printf("1 HOUR= %lld\n", (long long)HOUR);

        /* --- 2. duration_fmt edge cases --- */
        printf("\nduration_fmt:\n");
        printf("0          = \"" StrFmt "\"\n", StrArgs(duration_fmt(a, 0)));
        printf("500        = \"" StrFmt "\"\n", StrArgs(duration_fmt(a, 500)));
        printf("1*US       = \"" StrFmt "\"\n",
               StrArgs(duration_fmt(a, 1 * US)));
        printf("1*MS       = \"" StrFmt "\"\n",
               StrArgs(duration_fmt(a, 1 * MS)));
        printf("250*MS     = \"" StrFmt "\"\n",
               StrArgs(duration_fmt(a, 250 * MS)));
        printf("1500*MS    = \"" StrFmt "\"\n",
               StrArgs(duration_fmt(a, 1500 * MS)));
        printf("1*MIN      = \"" StrFmt "\"\n",
               StrArgs(duration_fmt(a, 1 * MIN)));
        printf("90*MIN     = \"" StrFmt "\"\n",
               StrArgs(duration_fmt(a, 90 * MIN)));
        printf("1*HOUR     = \"" StrFmt "\"\n",
               StrArgs(duration_fmt(a, 1 * HOUR)));
        printf("-500*MS    = \"" StrFmt "\"\n",
               StrArgs(duration_fmt(a, -500 * MS)));

        /* --- 3. instant_now and instant_since --- */
        printf("\ntiming:\n");
        Instant t0 = instant_now();
        /* burn some cycles */
        volatile uint64_t x = 0;
        for (int i = 0; i < 1000000; i++) x += (uint64_t)i;
        Duration elapsed = instant_since(t0);
        printf("loop elapsed: \"" StrFmt "\"  (x=%llu)\n",
               StrArgs(duration_fmt(a, elapsed)),
               (unsigned long long)x);

        /* --- 4. instant_diff --- */
        Instant  t1   = instant_now();
        Duration diff = instant_diff(t0, t1);
        printf("diff t0->t1: \"" StrFmt "\"  (positive=%d)\n",
               StrArgs(duration_fmt(a, diff)),
               diff > 0);

        /* negative diff */
        Duration neg_diff = instant_diff(t1, t0);
        printf("diff t1->t0: \"" StrFmt "\"  (negative=%d)\n",
               StrArgs(duration_fmt(a, neg_diff)),
               neg_diff < 0);

        /* --- 5. instant_add --- */
        Instant  future = instant_add(t1, 5 * SEC);
        Duration gap    = instant_diff(t1, future);
        printf("instant_add 5s: gap=\"" StrFmt "\"\n",
               StrArgs(duration_fmt(a, gap)));

        /* --- 6. datetime_utc and datetime_local --- */
        printf("\nDatetime:\n");
        Instant  now   = instant_now();
        DateTime utc   = datetime_utc(now);
        DateTime local = datetime_local(now);

        printf("UTC:    \"" StrFmt "\"\n", StrArgs(datetime_fmt(a, utc)));
        printf("local:  \"" StrFmt "\"\n", StrArgs(datetime_fmt(a, local)));
        printf("date:   \"" StrFmt "\"\n", StrArgs(datetime_fmt_date(a, utc)));
        printf("time:   \"" StrFmt "\"\n", StrArgs(datetime_fmt_time(a, utc)));

        printf("year=%d  month=%d  day=%d\n", utc.year, utc.month, utc.day);
        printf("hour=%d  min=%d   sec=%d\n", utc.hour, utc.min, utc.sec);
        printf("weekday=%d  yearday=%d\n", utc.weekday, utc.yearday);
        printf("nsec=%u  utc_offset_sec=%d\n", utc.nsec, utc.utc_offset_sec);

        /* --- 7. instant_from_datetime: round-trip --- */
        Instant  rt  = instant_from_datetime(utc);
        Duration err = instant_diff(now, rt);
        if (err < 0) err = -err;
        printf("round-trip error: \"" StrFmt "\" (should be < 2s)\n",
               StrArgs(duration_fmt(a, err)));

        /* --- 8. time_sleep --- */
        Instant before = instant_now();
        time_sleep(2 * MS);
        Duration slept = instant_since(before);
        printf("sleep 2ms, actual: \"" StrFmt "\"\n",
               StrArgs(duration_fmt(a, slept)));

        /* --- 9. Zero-duration sleep is safe --- */
        time_sleep(0);
        time_sleep(-100);
        printf("sleep(0) and sleep(-100) are safe\n");

        arena_free(a);
        printf("done.\n");
        return 0;
}
