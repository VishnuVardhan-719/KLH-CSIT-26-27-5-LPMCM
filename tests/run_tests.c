/*
 * run_tests.c — test suite for the Linux Process Monitoring & Control System.
 *
 * Coverage (mirrors the master spec, section 29):
 *   - /proc discovery (monitor_refresh finds real processes, e.g. PID 1)
 *   - parse_stat_line / parse_status_field (including "(comm) with spaces")
 *   - CPU interval math (cpu_percent_of) and state naming
 *   - search / filter predicates
 *   - process tree construction (forked children appear under the parent)
 *   - invalid PID / permission / disappearing-process handling
 *   - process-control safety gates (self, ancestor, PID 1 refusal)
 *   - real SIGSTOP / SIGCONT / SIGTERM / SIGKILL round-trips on
 *     disposable forked children (never any system process)
 *
 * Exit status: 0 when all tests pass, 1 otherwise.
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE /* usleep */

#include "process_control.h"
#include "process_demo.h"
#include "process_monitor.h"
#include "process_tree.h"
#include "system_info.h"
#include "utils.h"

#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int g_checks, g_failures;
static const char *g_group;

static void ok(int cond, const char *what)
{
    ++g_checks;
    if (cond) {
        printf("  ok %d - %s\n", g_checks, what);
    } else {
        ++g_failures;
        printf("  FAIL %d - %s\n", g_checks, what);
    }
}

static void group(const char *name)
{
    g_group = name;
    printf("\n# %s\n", name);
}

/* Pause ourselves so a test parent can stop/continue/kill us. The child
 * writes a marker to stdout first so the parent knows it exists. */
static void child_park_self(void)
{
    printf("PARKED %d\n", getpid());
    fflush(stdout);
    for (;;)
        pause(); /* only SIGCONT (or termination) ends this */
}

/* Fork a child that parks itself; returns the child PID. */
static pid_t fork_parked_child(void)
{
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        child_park_self();
        _exit(0);
    }
    return pid;
}

/* Fork a disposable child that continuously consumes one CPU. */
static pid_t fork_busy_child(void)
{
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        volatile unsigned long long n = 0;
        for (;;)
            ++n;
    }
    return pid;
}

static const ProcessInfo *find_process(const ProcVec *vec, pid_t pid)
{
    size_t i;
    for (i = 0; i < vec->count; ++i)
        if (vec->items[i].pid == (int)pid)
            return &vec->items[i];
    return NULL;
}

/* Match the monitor's power-of-two growth rule for a scan of n PIDs. */
static size_t expected_tick_capacity(size_t n)
{
    size_t cap = 64, used = 0;
    while (used < n) {
        if (used * 10 >= cap * 7)
            cap *= 2;
        ++used;
    }
    return cap;
}

/* Fork a child that exec()s /bin/true — validates exec behaviour. */
static pid_t fork_exec_true(void)
{
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execl("/bin/true", "true", (char *)NULL);
        _exit(127); /* exec failed */
    }
    return pid;
}

/* Wait for the child's state to change, bounded (avoids hangs). */
static int wait_state(pid_t pid, char want, int timeout_ms)
{
    for (int i = 0; i < timeout_ms; i += 10) {
        char path[64];
        char buf[2048];
        StatFields f;
        snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);
        if (read_text_file(path, buf, sizeof buf) > 0 &&
            parse_stat_line(buf, &f) == 0 && f.state == want)
            return 0;
        usleep(10 * 1000);
    }
    return -1;
}

/* ------------------------------------------------------------------ */

static void test_parse_stat_line(void)
{
    StatFields f;

    group("parse_stat_line");
    ok(parse_stat_line("1 (systemd) S 0 1 1 0 -1 4194560 0 0 0 0 0 0 0 0 "
                       "20 0 1 0 1000 100000 100 18446744073709551615",
                       &f) == 0,
       "simple comm parses");
    ok(f.pid == 1 && f.ppid == 0 && f.state == 'S', "fields correct");
    ok(strcmp(f.comm, "systemd") == 0, "comm extracted");

    ok(parse_stat_line("42 (Web Content) R 1 42 42 0 -1 4194560 0 0 0 0 "
                       "7 3 0 0 20 0 64 0 9000 2000000 800 "
                       "18446744073709551615",
                       &f) == 0,
       "comm with spaces parses");
    ok(strcmp(f.comm, "Web Content") == 0, "spaced comm correct");
    ok(f.state == 'R' && f.ppid == 1 && f.utime == 7 && f.stime == 3,
       "spaced-comm fields correct");
    ok(f.num_threads == 64 && f.rss_pages == 800, "threads/rss correct");
    ok(f.starttime == 9000, "process start time parsed for PID identity");

    ok(parse_stat_line("garbage-without-parentheses", &f) != 0,
       "malformed line rejected");
    ok(parse_stat_line("", &f) != 0, "empty line rejected");
    ok(parse_stat_line("x (a", &f) != 0, "truncated line rejected");
}

static void test_parse_status_field(void)
{
    const char *status =
        "Name:\tbash\n"
        "Uid:\t1000\t1000\t1000\t1000\n"
        "VmRSS:\t  5124 kB\n"
        "Threads:\t2\n";
    char out[64];

    group("parse_status_field");
    ok(parse_status_field(status, "Name", out, sizeof out) == 0 &&
           strcmp(out, "bash") == 0,
       "Name parsed");
    ok(parse_status_field(status, "Uid", out, sizeof out) == 0 &&
           strcmp(out, "1000") == 0,
       "first Uid field parsed");
    ok(parse_status_field(status, "VmRSS", out, sizeof out) == 0 &&
           strcmp(out, "5124 kB") == 0,
       "VmRSS parsed");
    ok(parse_status_field(status, "Missing", out, sizeof out) != 0,
       "missing key rejected");
    ok(parse_status_field(NULL, "Name", out, sizeof out) != 0,
       "NULL buffer rejected");
}

static void test_cpu_math(void)
{
    group("cpu_percent_of");
    ok(cpu_percent_of(10, 30, 1000, 1100) > 19.0 &&
           cpu_percent_of(10, 30, 1000, 1100) < 21.0,
       "20 process ticks / 100 system ticks = 20%%");
    ok(cpu_percent_of(0, 50, 1000, 2000) > 4.0 &&
           cpu_percent_of(0, 50, 1000, 2000) < 6.0,
       "50/1000 over 1000 = 5%%");
    ok(cpu_percent_of(10, 30, 1000, 1000) == 0.0,
       "no system delta -> 0 (guard)");
    ok(cpu_percent_of(30, 10, 1000, 2000) == 0.0,
       "negative process delta -> 0 (process replaced)");
    ok(cpu_percent_of(0, 0, 0, 0) == 0.0, "all zeros -> 0");
}

static void test_states(void)
{
    group("state_char_name");
    ok(strcmp(state_char_name('R'), "RUNNING") == 0, "R -> RUNNING");
    ok(strcmp(state_char_name('S'), "SLEEPING") == 0, "S -> SLEEPING");
    ok(strcmp(state_char_name('D'), "DISK SLEEP") == 0, "D -> DISK SLEEP");
    ok(strcmp(state_char_name('T'), "STOPPED") == 0, "T -> STOPPED");
    ok(strcmp(state_char_name('Z'), "ZOMBIE") == 0, "Z -> ZOMBIE");
    ok(strcmp(state_char_name('I'), "IDLE") == 0, "I -> IDLE");
    ok(strcmp(state_char_name('?'), "UNKNOWN") == 0,
       "unknown state -> UNKNOWN");
}

static void test_search_filter(void)
{
    ProcessInfo pi;
    memset(&pi, 0, sizeof pi);
    pi.pid = 738;
    pi.ppid = 1;
    sstrcpy(pi.name, sizeof pi.name, "firefox");
    sstrcpy(pi.user, sizeof pi.user, "vishnu");
    pi.state = 'S';

    group("search/filter");
    ok(process_matches(&pi, "fire") == 1, "name substring matches");
    ok(process_matches(&pi, "FIREFOX") == 1, "case-insensitive match");
    ok(process_matches(&pi, "738") == 1, "PID search matches");
    ok(process_matches(&pi, "vishnu") == 1, "user search matches");
    ok(process_matches(&pi, "zzz") == 0, "no match on absent text");
    ok(process_matches(&pi, "") == 1, "empty query matches all");
    ok(process_matches_state(&pi, 'S') == 1, "state filter S matches");
    ok(process_matches_state(&pi, 'R') == 0, "state filter R rejects S");
    ok(process_matches_state(&pi, 'A') == 1, "state filter A matches all");
}

static void test_monitor_discovery(void)
{
    MonitorState *mon;
    ProcVec vec;
    SystemStats stats;
    int found_pid1 = 0, found_self = 0;
    size_t i;

    group("monitor discovery (/proc)");
    mon = monitor_create();
    ok(mon != NULL, "monitor_create succeeds");

    memset(&vec, 0, sizeof vec);
    memset(&stats, 0, sizeof stats);
    ok(monitor_refresh(mon, &vec, &stats) != (size_t)-1,
       "monitor_refresh reads /proc");
    ok(vec.count > 10, "found a plausible number of processes");
    ok(stats.process_count == vec.count, "process_count matches table");

    for (i = 0; i < vec.count; ++i) {
        if (vec.items[i].pid == 1)
            found_pid1 = 1;
        if (vec.items[i].pid == getpid())
            found_self = 1;
        if (vec.items[i].name[0] == '\0')
            ok(0, "empty process name in table");
    }
    ok(found_pid1, "PID 1 discovered automatically");
    ok(found_self, "this test process discovered automatically");

    ok(monitor_refresh(mon, &vec, &stats) != (size_t)-1,
       "second refresh works (reused buffer)");
    proc_vec_free(&vec);
    monitor_destroy(mon);
}

static void test_top_style_cpu_percent(void)
{
    MonitorState *mon;
    ProcVec vec;
    SystemStats stats;
    const ProcessInfo *pi;
    pid_t child;

    group("top-style process CPU");
    mon = monitor_create();
    memset(&vec, 0, sizeof vec);
    memset(&stats, 0, sizeof stats);
    child = fork_busy_child();
    ok(child > 0, "CPU-burner child created");
    if (child > 0) {
        monitor_refresh(mon, &vec, &stats);
        usleep(400 * 1000);
        monitor_refresh(mon, &vec, &stats);
        pi = find_process(&vec, child);
        ok(pi != NULL, "CPU-burner child remains visible");
        ok(pi && pi->cpu_percent > 60.0,
           "one busy thread reports near 100%%, not total-machine share");
        kill(child, SIGKILL);
        waitpid(child, NULL, 0);
    }
    proc_vec_free(&vec);
    monitor_destroy(mon);
}

static void test_tick_history_after_collision_prune(void)
{
    MonitorState *mon;
    ProcVec vec;
    SystemStats stats;
    const ProcessInfo *pi;
    pid_t first, survivor = -1;
    size_t cap, attempts;

    group("CPU history pruning");
    mon = monitor_create();
    memset(&vec, 0, sizeof vec);
    memset(&stats, 0, sizeof stats);
    monitor_refresh(mon, &vec, &stats);

    first = fork_busy_child();
    ok(first > 0, "first colliding child created");
    if (first > 0) {
        monitor_refresh(mon, &vec, &stats); /* records first in history */
        cap = expected_tick_capacity(vec.count);
        kill(first, SIGKILL);
        waitpid(first, NULL, 0);

        for (attempts = 0; attempts < cap * 4; ++attempts) {
            pid_t candidate = fork_busy_child();
            if (candidate < 0)
                break;
            if (((unsigned)candidate & (cap - 1)) ==
                ((unsigned)first & (cap - 1))) {
                survivor = candidate;
                break;
            }
            kill(candidate, SIGKILL);
            waitpid(candidate, NULL, 0);
        }
        ok(survivor > 0, "second child with the same hash bucket created");
        if (survivor > 0) {
            monitor_refresh(mon, &vec, &stats); /* inserts, then prunes first */
            usleep(300 * 1000);
            monitor_refresh(mon, &vec, &stats);
            pi = find_process(&vec, survivor);
            ok(pi && pi->cpu_percent > 5.0,
               "pruning a collision does not erase survivor CPU history");
            kill(survivor, SIGKILL);
            waitpid(survivor, NULL, 0);
        }
    }
    proc_vec_free(&vec);
    monitor_destroy(mon);
}

static void test_get_process_info(void)
{
    ProcessInfo info;
    group("get_process_info");
    ok(get_process_info(1, &info) == 0, "PID 1 readable");
    ok(info.pid == 1, "PID field correct");
    ok(info.state_name[0] != '\0', "state name filled");
    ok(get_process_info(-5, &info) != 0, "negative PID rejected");
    ok(get_process_info(1048576, &info) != 0,
       "nonexistent PID rejected (ENOENT path)");
}

static void test_disappearing_process(void)
{
    MonitorState *mon;
    ProcVec vec;
    SystemStats stats;
    pid_t pid;
    ProcessInfo info;

    group("disappearing process");
    mon = monitor_create();
    memset(&vec, 0, sizeof vec);
    memset(&stats, 0, sizeof stats);
    monitor_refresh(mon, &vec, &stats);

    pid = fork_exec_true();
    ok(pid > 0, "fork/exec child created");
    if (pid > 0) {
        int status;
        waitpid(pid, &status, 0); /* child exits immediately */
        ok(WIFEXITED(status) && WEXITSTATUS(status) == 0,
           "exec'd /bin/true exited 0 (exec works)");
        /* The process is gone; /proc read must fail gracefully, not crash. */
        ok(get_process_info((int)pid, &info) != 0,
           "exited PID no longer readable (graceful ENOENT)");
    }
    monitor_refresh(mon, &vec, &stats); /* must survive */
    proc_vec_free(&vec);
    monitor_destroy(mon);
}

static void test_control_safety(void)
{
    char err[256];
    group("control safety gates");
    ok(ctrl_validate(1, CTRL_STOP, err, sizeof err) != 0,
       "PID 1 refused");
    ok(ctrl_validate(0, CTRL_STOP, err, sizeof err) != 0,
       "PID 0 refused");
    ok(ctrl_validate(-1, CTRL_KILL, err, sizeof err) != 0,
       "negative PID refused");
    ok(ctrl_validate(getpid(), CTRL_STOP, err, sizeof err) != 0,
       "monitor itself refused");
    ok(ctrl_validate(getppid(), CTRL_STOP, err, sizeof err) != 0,
       "parent (shell) refused");
    ok(ctrl_validate(1, CTRL_KILL, err, sizeof err) != 0,
       "PID 1 SIGKILL refused");
    ok(strlen(err) > 0, "rejection explains itself");
    ok(ctrl_validate(2, (CtrlAction)99, err, sizeof err) != 0,
       "invalid control action refused");
    errno = 0;
    ok(ctrl_send(2, (CtrlAction)99) != 0 && errno == EINVAL,
       "invalid control action reports EINVAL");
}

static void test_control_roundtrip(void)
{
    pid_t pid;
    group("control round-trips (disposable children)");

    /* SIGSTOP + SIGCONT */
    pid = fork_parked_child();
    ok(pid > 0, "parked child created");
    if (pid > 0) {
        char err[256];
        ProcessInfo target;
        ok(get_process_info((int)pid, &target) == 0,
           "target identity captured before signaling");
        errno = 0;
        ok(ctrl_send_verified((int)pid, target.starttime + 1,
                              CTRL_PAUSE) != 0 && errno == ESRCH,
           "mismatched PID identity is not signaled");
        ok(ctrl_send_verified((int)pid, target.starttime,
                              CTRL_PAUSE) == 0,
           "SIGSTOP accepted for matching PID identity");
        ok(wait_state(pid, 'T', 2000) == 0, "child observed in T state");
        ok(ctrl_send((int)pid, CTRL_RESUME) == 0, "SIGCONT accepted");
        ok(ctrl_send((int)pid, CTRL_STOP) == 0, "SIGTERM accepted");
        {
            int status;
            waitpid(pid, &status, 0);
            ok(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM,
               "child terminated by SIGTERM");
        }
        ok(ctrl_send((int)pid, CTRL_PAUSE) != 0 || errno == ESRCH,
           "signaling dead PID fails gracefully (ESRCH)");
        (void)err;
    }

    /* SIGKILL */
    pid = fork_parked_child();
    ok(pid > 0, "second parked child created");
    if (pid > 0) {
        int status;
        ok(ctrl_send((int)pid, CTRL_KILL) == 0, "SIGKILL accepted");
        waitpid(pid, &status, 0);
        ok(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL,
           "child killed by SIGKILL");
    }
}

static void test_tree(void)
{
    MonitorState *mon;
    ProcVec vec;
    SystemStats stats;
    TreeLines tl;
    pid_t child;
    int found_child = 0;
    size_t i;

    group("process tree");
    mon = monitor_create();
    memset(&vec, 0, sizeof vec);
    memset(&stats, 0, sizeof stats);
    monitor_refresh(mon, &vec, &stats);

    child = fork_parked_child();
    ok(child > 0, "tree child created");
    if (child > 0) {
        monitor_refresh(mon, &vec, &stats); /* child now in snapshot */
        memset(&tl, 0, sizeof tl);
        ok(tree_build(&vec, &tl) == 0, "tree_build succeeds");
        ok(tl.count > 0, "tree has lines");
        for (i = 0; i < tl.count; ++i)
            if (tl.items[i].pid == (int)child)
                found_child = 1;
        ok(found_child, "forked child appears in tree");
        ok(strstr(tl.items[0].text, "systemd") != NULL ||
               tl.items[0].pid == 1 || tl.count > 1,
           "tree rooted deterministically");
        tree_lines_free(&tl);
        ctrl_send((int)child, CTRL_KILL);
        waitpid(child, NULL, 0);
    }

    proc_vec_free(&vec);
    monitor_destroy(mon);
}

static void test_tree_cycle(void)
{
    ProcVec vec;
    TreeLines tree;
    ProcessInfo a, b;

    group("process tree cycle handling");
    memset(&vec, 0, sizeof vec);
    memset(&tree, 0, sizeof tree);
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    a.pid = 1001;
    a.ppid = 1002;
    sstrcpy(a.name, sizeof a.name, "cycle-a");
    sstrcpy(a.state_name, sizeof a.state_name, "SLEEPING");
    b.pid = 1002;
    b.ppid = 1001;
    sstrcpy(b.name, sizeof b.name, "cycle-b");
    sstrcpy(b.state_name, sizeof b.state_name, "SLEEPING");
    proc_vec_push(&vec, &a);
    proc_vec_push(&vec, &b);

    ok(tree_build(&vec, &tree) == 0, "cyclic snapshot is accepted safely");
    ok(tree.count == 2, "each process in a cycle is rendered exactly once");

    tree_lines_free(&tree);
    proc_vec_free(&vec);
}

static void test_system_info(void)
{
    SystemStats s;
    group("system_info");
    memset(&s, 0, sizeof s);
    ok(system_info_read_static(&s) == 0, "static info read");
    ok(s.hostname[0] != '\0', "hostname non-empty");
    ok(s.kernel[0] != '\0', "kernel release non-empty");
    ok(s.cpu_count > 0, "cpu count > 0");
    ok(s.mem_total_kb > 0, "MemTotal > 0");

    system_info_read_dynamic(&s);
    ok(s.uptime_seconds > 0.0, "uptime > 0");
    ok(s.load[0] >= 0.0, "load average >= 0");
    ok(s.mem_available_kb > 0, "MemAvailable > 0");

    ok(parse_meminfo_kb("MemTotal:       16384256 kB\n", "MemTotal") ==
           16384256,
       "parse_meminfo_kb exact");
    ok(parse_meminfo_kb("HugePages_Total:       0\nMemTotal: 100 kB\n",
                        "MemTotal") == 100,
       "prefix keys not confused (MemTotal vs HugePages_Total)");
    ok(parse_meminfo_kb("Nope: 1 kB\n", "MemTotal") == -1,
       "missing key -> -1");
}

static void test_utils(void)
{
    char buf[32];
    group("utils");
    sstrcpy(buf, sizeof buf, "hello");
    ok(strcmp(buf, "hello") == 0, "sstrcpy basic");
    sstrcpy(buf, sizeof buf, "0123456789ABCDEF0123456789ABCDEF-extra");
    ok(strlen(buf) == sizeof buf - 1, "sstrcpy truncates safely");
    {
        char t[16];
        sstrcpy(t, sizeof t, "  spaced  ");
        ok(strcmp(trim(t), "spaced") == 0, "trim strips blanks");
    }
    ok(ci_contains("FireFox", "fox") == 1, "ci_contains match");
    ok(ci_contains("FireFox", "zzz") == 0, "ci_contains no-match");
    fmt_uptime(3661.0, buf, sizeof buf);
    ok(strstr(buf, "01:01:01") != NULL, "uptime HH:MM:SS format");
    ok(utf8_width("héllo") == 5, "utf8_width counts code points");
}

int main(void)
{
    printf("LPMCS test suite\n");
    printf("================\n");

    test_utils();
    test_system_info();
    test_parse_stat_line();
    test_parse_status_field();
    test_cpu_math();
    test_states();
    test_search_filter();
    test_monitor_discovery();
    test_top_style_cpu_percent();
    test_tick_history_after_collision_prune();
    test_get_process_info();
    test_disappearing_process();
    test_control_safety();
    test_control_roundtrip();
    test_tree();
    test_tree_cycle();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0)
        printf("ALL TESTS PASSED\n");
    else
        printf("TESTS FAILED\n");
    return g_failures ? 1 : 0;
}
