#include "run_tests.h"
#include "process_monitor.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* /proc/<PID>/stat parsing                                            */
/* ------------------------------------------------------------------ */

static void test_stat_basic(void)
{
    printf("stat parsing: basic fields\n");

    /* synthetic stat content with spaces inside (comm) */
    static const char STAT[] =
        "1234 (some chrome Browser) R 900 0 0 0 0 0 0 0 0 0 "
        "77 33 0 0 20 0 4 0 12345 12345678 512 18446744073709551615 "
        "1 1 0 0 0 0 0 0 0 0 0 0 17 1 0 0 0 0 0";

    ProcessInfo p;
    char err[PM_ERR_MAX];
    t_check_int(pm_parse_stat(STAT, &p, err, sizeof(err)), 0, "stat parses");
    t_check_int(p.pid, 1234, "stat pid");
    t_check(strcmp(p.name, "some chrome Browser") == 0, "stat comm keeps spaces");
    t_check(p.state == 'R', "stat state");
    t_check_int(p.ppid, 900, "stat ppid");
    t_check_int((long long)p.utime, 77, "stat utime");
    t_check_int((long long)p.stime, 33, "stat stime");
    t_check_int(p.priority, 20, "stat priority");
    t_check_int(p.nice, 0, "stat nice");
    t_check_int(p.threads, 4, "stat threads");
    t_check_int((long long)p.rss_pages, 512, "stat rss");
}

static void test_stat_malformed(void)
{
    printf("stat parsing: malformed input rejected\n");

    ProcessInfo p;
    char err[PM_ERR_MAX];

    t_check_int(pm_parse_stat("", &p, err, sizeof(err)), -1, "empty rejected");
    t_check_int(pm_parse_stat("100 (weird", &p, err, sizeof(err)), -1,
                "missing ) rejected");
    t_check_int(pm_parse_stat("abc (x) S 1", &p, err, sizeof(err)), -1,
                "non-numeric pid rejected");
    t_check_int(pm_parse_stat("100 (x) S 1 2", &p, err, sizeof(err)), -1,
                "truncated stat rejected");
}

static void test_status_fields(void)
{
    printf("status parsing: field lookup\n");

    static const char STATUS[] =
        "Name:\tfirefox\n"
        "Uid:\t1000\t1000\t1000\t1000\n"
        "Threads:\t42\n"
        "VmRSS:\t  123456 kB\n";

    char value[128];
    t_check(pm_parse_status_field(STATUS, "Uid", value, sizeof(value)),
            "Uid found");
    t_check(strncmp(value, "1000", 4) == 0, "Uid value");
    t_check(pm_parse_status_field(STATUS, "Threads", value, sizeof(value)),
            "Threads found");
    t_check(strcmp(value, "42") == 0, "Threads value");
    t_check(!pm_parse_status_field(STATUS, "Nonexistent", value, sizeof(value)),
            "missing key not found");
    t_check(!pm_parse_status_field(STATUS, "Uid2", value, sizeof(value)),
            "prefix key not matched");
}

/* ------------------------------------------------------------------ */
/* CPU interval math                                                   */
/* ------------------------------------------------------------------ */

static void test_cpu_delta(void)
{
    printf("cpu math: interval percentages\n");

    /* 100 ticks at 100 ticks/s = 1.0 s CPU over a 2.0 s wall interval */
    double pct = pm_cpu_percent_delta(1000, 1100, 10.0, 12.0, 100);
    t_check(pct > 49.9 && pct < 50.1, "50% when half busy");

    /* multi-core: process used 4 cores fully for 1 s (wall 1 s) */
    pct = pm_cpu_percent_delta(0, 400, 0.0, 1.0, 100);
    t_check(pct > 399.9 && pct < 400.1, "400% on 4 busy cores");

    /* no previous sample: zero */
    pct = pm_cpu_percent_delta(0, 100, 0.0, 0.0, 100);
    t_check(pct == 0.0, "zero when wall interval is zero");

    /* counter went backwards (process restarted): zero, never negative */
    pct = pm_cpu_percent_delta(500, 100, 10.0, 11.0, 100);
    t_check(pct == 0.0, "zero when ticks decreased");
}

/* ------------------------------------------------------------------ */
/* State names and search                                              */
/* ------------------------------------------------------------------ */

static void test_states_and_search(void)
{
    printf("states and search\n");

    char buf[64];
    pm_state_text('R', buf, sizeof(buf));
    t_check(strcmp(buf, "RUNNING (R)") == 0, "RUNNING (R) text");
    pm_state_text('Z', buf, sizeof(buf));
    t_check(strcmp(buf, "ZOMBIE (Z)") == 0, "ZOMBIE (Z) text");

    ProcessInfo p;
    memset(&p, 0, sizeof(p));
    p.pid = 4242;
    utils_str_copy(p.name, sizeof(p.name), "firefox");
    utils_str_copy(p.user, sizeof(p.user), "vishnu");
    utils_str_copy(p.cmdline, sizeof(p.cmdline), "/usr/lib/firefox/firefox");

    t_check(pm_matches_search(&p, "fire"), "search by name fragment");
    t_check(pm_matches_search(&p, "FIREFOX"), "search case-insensitive");
    t_check(pm_matches_search(&p, "4242"), "search by pid");
    t_check(pm_matches_search(&p, "vish"), "search by user");
    t_check(pm_matches_search(&p, "lib/fire"), "search in cmdline");
    t_check(!pm_matches_search(&p, "python"), "non-match rejected");
    t_check(pm_matches_search(&p, ""), "empty query matches all");

    t_check(pm_matches_state(&p, '\0'), "no state filter matches all");
    t_check(!pm_matches_state(&p, 'S'), "state filter rejects");
}

void run_parsing_tests(void)
{
    test_stat_basic();
    test_stat_malformed();
    test_status_fields();
    test_cpu_delta();
    test_states_and_search();
}
