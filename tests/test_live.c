#include "run_tests.h"
#include "process_control.h"
#include "process_demo.h"
#include "process_monitor.h"
#include "utils.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void msleep_test(int ms)
{
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

/* ------------------------------------------------------------------ */
/* Live /proc discovery                                                */
/* ------------------------------------------------------------------ */

static void test_live_monitor(void)
{
    printf("live: /proc discovery and interval CPU\n");

    ProcessMonitor m;
    char err[PM_ERR_MAX];
    t_check_int(monitor_init(&m, err, sizeof(err)), 0, "monitor_init");

    /* first scan establishes the baseline */
    t_check_int(monitor_refresh(&m, err, sizeof(err)), 0, "first refresh");

    size_t first_count = m.current.count;
    t_check(first_count > 2, "found a realistic number of processes");

    /* find our own process: it must exist with a valid state and user */
    bool found_self = false;
    for (size_t i = 0; i < first_count; i++) {
        if (m.current.procs[i].pid == getpid()) {
            found_self = true;
            t_check(m.current.procs[i].state != '?', "self has a state");
            t_check(m.current.procs[i].user[0] != '\0', "self has a user");
            t_check(m.current.procs[i].ppid > 0, "self has a parent");
        }
    }
    t_check(found_self, "monitor sees its own process");

    /* wait for the sampling interval, then refresh again */
    msleep_test(300);
    t_check_int(monitor_refresh(&m, err, sizeof(err)), 0, "second refresh");
    t_check(m.current.count > 0, "second refresh found processes");

    /* every CPU% must be sane (multi-core machines can exceed 100) */
    bool sane_cpu = true;
    for (size_t i = 0; i < m.current.count; i++)
        if (m.current.procs[i].cpu_percent < 0.0 ||
            m.current.procs[i].cpu_percent > 100.0 * m.sys.cpu_count + 1.0)
            sane_cpu = false;
    t_check(sane_cpu, "all cpu percentages within bounds");

    /* system stats are real */
    t_check(m.sys.cpu_count >= 1, "cpu count");
    t_check(m.sys.mem_total_mb > 0, "total memory read");
    t_check(m.sys.uptime_seconds > 0, "uptime read");

    monitor_free(&m);
}

static void test_disappearing_process(void)
{
    printf("live: reading a vanished PID fails gracefully\n");

    ProcessInfo info;
    char err[PM_ERR_MAX];
    int rc = pm_read_process(4000000, NULL, &info, err, sizeof(err));
    t_check(rc != 0, "bogus PID rejected");
    t_check(err[0] != '\0', "error message present");

    /* PID 0 and negative are invalid too */
    t_check(pm_read_process(0, NULL, &info, err, sizeof(err)) != 0,
            "PID 0 rejected");
    t_check(pm_read_process(-5, NULL, &info, err, sizeof(err)) != 0,
            "negative PID rejected");
}

/* ------------------------------------------------------------------ */
/* Safety policy                                                       */
/* ------------------------------------------------------------------ */

static void test_safety_refusals(void)
{
    printf("safety: protected processes are refused\n");

    char err[PC_ERR_MAX];

    t_check(pcontrol_is_protected(1), "PID 1 protected");
    t_check(pcontrol_is_protected(0), "PID 0 protected");
    t_check(pcontrol_is_protected(-3), "negative PID protected");
    t_check(pcontrol_is_protected(getpid()), "own PID protected");
    t_check(pcontrol_is_protected(getppid()), "parent PID protected");
    t_check(!pcontrol_is_protected(12345), "random PID not protected");

    t_check_int(pcontrol_send(1, SIGTERM, err, sizeof(err)), PC_ERR_REFUSED,
                "SIGTERM to init refused");
    t_check_int(pcontrol_send(getpid(), SIGKILL, err, sizeof(err)),
                PC_ERR_REFUSED, "SIGKILL to self refused");
    t_check_int(pcontrol_send(99999999, SIGTERM, err, sizeof(err)),
                PC_ERR_INVALID, "absurd PID rejected");
    t_check_int(pcontrol_send(12345, 999, err, sizeof(err)), PC_ERR_INVALID,
                "unsupported signal rejected");

    /* nonexistent but permitted PID -> ESRCH mapping */
    t_check_int(pcontrol_send(4000000, SIGTERM, err, sizeof(err)),
                PC_ERR_NO_PROCESS, "ESRCH mapped");
    t_check(strstr(err, "does not exist") != NULL, "ESRCH message");
}

/* ------------------------------------------------------------------ */
/* Signal lifecycle on a disposable child                              */
/* ------------------------------------------------------------------ */

static void test_signal_lifecycle(void)
{
    printf("signals: stop/continue/terminate on our own child\n");

    pid_t pid = fork();
    if (pid < 0) {
        t_check(false, "fork for signal test");
        return;
    }
    if (pid == 0) {
        /* disposable child: sleep quietly until signaled */
        for (;;)
            pause();
        _exit(0);
    }

    char err[PC_ERR_MAX];
    char msg[128];
    ProcessInfo info;

    /* allow a moment for the child to appear in /proc */
    msleep_test(100);

    /* PAUSE */
    t_check_int(pcontrol_send_action(pid, PC_SIG_PAUSE, err, sizeof(err)),
                PC_OK, "SIGSTOP delivered");
    t_check(pcontrol_verify(pid, PC_SIG_PAUSE, msg, sizeof(msg)),
            "child observed STOPPED");
    t_check_int(pm_read_process(pid, NULL, &info, err, sizeof(err)), 0,
                "stopped child still in /proc");
    t_check(info.state == 'T' || info.state == 't', "state is T");

    /* RESUME */
    t_check_int(pcontrol_send_action(pid, PC_SIG_RESUME, err, sizeof(err)),
                PC_OK, "SIGCONT delivered");
    t_check(pcontrol_verify(pid, PC_SIG_RESUME, msg, sizeof(msg)),
            "child observed resumed");

    /* TERMINATE */
    t_check_int(pcontrol_send_action(pid, PC_SIG_STOP, err, sizeof(err)),
                PC_OK, "SIGTERM delivered");
    t_check(pcontrol_verify(pid, PC_SIG_STOP, msg, sizeof(msg)),
            "termination verified");

    /* SIGTERM is not blockable by default; the child should be gone */
    int status = 0;
    pid_t r = waitpid(pid, &status, 0);
    t_check(r == pid, "waitpid reaped the child");
    if (r == pid) {
        t_check(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM,
                "child died from SIGTERM");
    }

    /* after exit, verification reports "no longer running" */
    t_check(pcontrol_verify(pid, PC_SIG_STOP, msg, sizeof(msg)),
            "verify reports process gone");
}

/* ------------------------------------------------------------------ */
/* fork/exec/waitpid demo (safe, self-contained)                       */
/* ------------------------------------------------------------------ */

static void test_fork_exec_waitpid(void)
{
    printf("demo: fork() -> execl() -> waitpid()\n");

    /* exercise the same mechanics the demo screen uses */
    pid_t pid = fork();
    if (pid < 0) {
        t_check(false, "fork");
        return;
    }
    if (pid == 0) {
        execl("/bin/echo", "echo", "test-child-output", (char *)NULL);
        _exit(127); /* exec failed */
    }

    int status = 0;
    pid_t r = waitpid(pid, &status, 0);
    t_check(r == pid, "waitpid returned the child");
    t_check(WIFEXITED(status), "child exited normally");
    t_check_int(WEXITSTATUS(status), 0, "child exit status is 0");
}

void run_live_tests(void)
{
    test_live_monitor();
    test_disappearing_process();
    test_safety_refusals();
    test_signal_lifecycle();
    test_fork_exec_waitpid();
}
