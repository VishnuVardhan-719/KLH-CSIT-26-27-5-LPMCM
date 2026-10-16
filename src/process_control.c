/*
 * process_control.c — signal dispatch and safety validation.
 *
 * Safety model (project constraint §17):
 *   - PID 1 is never signalled (it is init; signalling it can panic or
 *     destabilise the whole system).
 *   - The monitor never signals itself or any of its ancestors, so the
 *     terminal session can never be destroyed by a stray keypress.
 *   - All signals go through kill(2) directly — no system("kill ...").
 *   - Errors are translated to explicit messages; nothing fails silently.
 */
#include "process_control.h"
#include "process_monitor.h"
#include "utils.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *ctrl_action_name(CtrlAction a)
{
    switch (a) {
    case CTRL_PAUSE:  return "Pause";
    case CTRL_RESUME: return "Resume";
    case CTRL_STOP:   return "Stop";
    case CTRL_KILL:   return "Force kill";
    }
    return "?";
}

const char *ctrl_signal_name(CtrlAction a)
{
    switch (a) {
    case CTRL_PAUSE:  return "SIGSTOP";
    case CTRL_RESUME: return "SIGCONT";
    case CTRL_STOP:   return "SIGTERM";
    case CTRL_KILL:   return "SIGKILL";
    }
    return "?";
}

int ctrl_action_signal(CtrlAction a)
{
    switch (a) {
    case CTRL_PAUSE:  return SIGSTOP;
    case CTRL_RESUME: return SIGCONT;
    case CTRL_STOP:   return SIGTERM;
    case CTRL_KILL:   return SIGKILL;
    }
    return 0;
}

/* Walk the PPid chain of the monitor to build the set of protected
 * ancestors (bounded depth; /proc reads are best-effort here). */
static int is_ancestor_of_self(int pid)
{
    char path[96], buf[4096];
    int cur = getpid();
    int depth;
    for (depth = 0; depth < 32 && cur > 1; ++depth) {
        int ppid = -1;
        snprintf(path, sizeof path, "/proc/%d/stat", cur);
        if (read_text_file(path, buf, sizeof buf) < 0)
            return 0;
        {
            const char *after = strrchr(buf, ')');
            if (!after)
                return 0;
            if (sscanf(after + 1, " %*c %d %*d", &ppid) < 1)
                return 0;
        }
        if (ppid == pid)
            return 1;
        cur = ppid;
    }
    return 0;
}

int ctrl_validate(int pid, CtrlAction a, char *err, size_t errsz)
{
    if (err && errsz)
        err[0] = '\0';
    if (ctrl_action_signal(a) == 0) {
        if (err && errsz)
            snprintf(err, errsz, "Invalid process-control action");
        return -1;
    }
    if (pid <= 1) {
        snprintf(err, errsz, "PID %d is protected (init/system)", pid);
        return -1;
    }
    if (pid == getpid()) {
        snprintf(err, errsz,
                 "Refusing to signal the monitor itself (PID %d)", pid);
        return -1;
    }
    if (is_ancestor_of_self(pid)) {
        snprintf(err, errsz,
                 "Refusing to signal PID %d (ancestor of the monitor)",
                 pid);
        return -1;
    }
    return 0;
}

int ctrl_send(int pid, CtrlAction a)
{
    int sig = ctrl_action_signal(a);
    if (sig == 0) {
        errno = EINVAL;
        return -1;
    }
    return kill(pid, sig); /* exactly 0 or -1 with errno set */
}

int ctrl_send_verified(int pid, unsigned long long expected_starttime,
                       CtrlAction a)
{
    ProcessInfo current;
    if (get_process_info(pid, &current) != 0 ||
        current.starttime != expected_starttime) {
        errno = ESRCH;
        return -1;
    }
    return ctrl_send(pid, a);
}

void ctrl_describe_failure(int pid, CtrlAction a, char *msg, size_t msgsz)
{
    if (!msg || msgsz == 0)
        return;
    switch (errno) {
    case ESRCH:
        snprintf(msg, msgsz, "Process %d no longer exists", pid);
        break;
    case EPERM:
        snprintf(msg, msgsz,
                 "Permission denied: cannot send %s to PID %d "
                 "(process is owned by another user)",
                 ctrl_signal_name(a), pid);
        break;
    case EINVAL:
        snprintf(msg, msgsz, "Internal error: invalid signal for PID %d",
                 pid);
        break;
    default:
        snprintf(msg, msgsz, "kill(%d, %s) failed: %s", pid,
                 ctrl_signal_name(a), strerror(errno));
        break;
    }
}
