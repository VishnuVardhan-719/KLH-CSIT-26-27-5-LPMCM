/*
 * process_control.h — signal-based process control with safety validation.
 *
 * Actions are validated BEFORE any signal is sent: the monitor refuses to
 * signal itself, its ancestors, or PID 1. SIGKILL additionally requires an
 * interactive confirmation handled by the UI layer.
 */
#ifndef LPMCS_PROCESS_CONTROL_H
#define LPMCS_PROCESS_CONTROL_H

#include <stddef.h>

typedef enum {
    CTRL_PAUSE = 0,  /* SIGSTOP  */
    CTRL_RESUME,     /* SIGCONT  */
    CTRL_STOP,       /* SIGTERM  */
    CTRL_KILL        /* SIGKILL  */
} CtrlAction;

/* Human names for menus and messages. */
const char *ctrl_action_name(CtrlAction a);   /* "Pause", "Force kill" ... */
const char *ctrl_signal_name(CtrlAction a);   /* "SIGSTOP", "SIGKILL" ...  */
int ctrl_action_signal(CtrlAction a);         /* SIGSTOP ... SIGKILL       */

/*
 * Safety gate. Returns 0 when the action may proceed, -1 otherwise, with a
 * clear reason written into err (e.g. "PID 1 (init) is protected").
 *
 * Refused unconditionally:
 *   - pid <= 1 (init and invalid)
 *   - the monitor itself (would freeze/kill this application)
 *   - any ancestor of the monitor (would destroy the shell session)
 */
int ctrl_validate(int pid, CtrlAction a, char *err, size_t errsz);

/*
 * Send the signal with kill(2). Returns 0 on success, -1 on failure with
 * errno preserved (ESRCH: already gone, EPERM: not permitted, ...).
 * This is the only place in the program that sends control signals.
 */
int ctrl_send(int pid, CtrlAction a);

/* Re-read /proc immediately before signaling and refuse if the PID now
 * belongs to a different process instance. */
int ctrl_send_verified(int pid, unsigned long long expected_starttime,
                       CtrlAction a);

/* Map a failed ctrl_send() to a user-facing message. */
void ctrl_describe_failure(int pid, CtrlAction a, char *msg, size_t msgsz);

#endif /* LPMCS_PROCESS_CONTROL_H */
