# Process Control Design

## Actions

The control layer exposes exactly four actions, all delivered by
calling `kill(pid, sig)` directly — no `system("kill ...")`, no shell:

| Action | Signal | Expected effect |
|---|---|---|
| Pause | `SIGSTOP` | Process enters state `T` (STOPPED) |
| Resume | `SIGCONT` | Process leaves `T`, becomes runnable again |
| Stop | `SIGTERM` | Polite termination request; process may clean up |
| Force kill | `SIGKILL` | Immediate, uncatchable termination |

`process_control.c` is the only file in the program that sends control
signals. The UI never calls `kill()` itself.

## Safety policy (enforced before every send)

`ctrl_validate()` runs before any signal and refuses, in order:

1. **PID range** — `pid <= 1` is rejected: init/systemd (PID 1) and
   invalid PIDs in one gate ("PID N is protected (init/system)").
2. **Self** — the monitor never signals its own PID
   ("Refusing to signal the monitor itself (PID N)").
3. **Ancestors** — the monitor walks its own PPID chain through
   /proc and refuses to signal any ancestor, which protects the
   shell session that launched it
   ("Refusing to signal PID N (ancestor of the monitor)").

Every refusal produces an explanatory message. Immediately before
delivery, `ctrl_send_verified()` re-reads `/proc/<PID>/stat` and compares
field 22 (`starttime`) with the selected snapshot. If the PID vanished or
was reused, the signal is refused with `ESRCH`.

## Failure mapping

`ctrl_send()` returns exactly what `kill()` returned. On failure,
`ctrl_describe_failure()` maps `errno` to a user-facing message:

| errno | Message shown to the user |
|---|---|
| `ESRCH` | "Process N no longer exists" |
| `EPERM` | "Permission denied: cannot send SIGxxx to PID N (process is owned by another user)" |
| `EINVAL` | "Internal error: invalid signal for PID N" |
| other | "kill(N, SIGxxx) failed: <strerror(errno)>" |

## Confirmation for SIGKILL

Force-kill is the one irreversible action, so it always routes through
a confirmation prompt (y/N — Enter or anything else cancels):

```
============================================================
 WARNING: FORCE TERMINATION
============================================================
 Process : <name read from /proc>
 PID     : <pid>
 Signal  : SIGKILL

 This action cannot be undone.
 Continue? [y/N]
```

SIGSTOP/SIGCONT/SIGTERM do not require confirmation because they are
recoverable (SIGCONT resumes, a SIGTERM'd process may clean up and
exit, and the user can restart what they stopped).

## Post-signal verification and refresh

After every successful `kill()`, the UI immediately re-reads the
process from /proc (`get_process_info`) and reports the observed
reality, then refreshes the whole table:

- **Pause**: success when the state letter becomes `T`
  ("Process N (name) paused - state STOPPED (T)").
- **Resume**: success when the state is no longer `T`
  ("Process N (name) resumed - state SLEEPING (S)").
- **Stop/Kill**: if the re-read fails, the process is gone
  ("Process N (name) is no longer running") — a normal outcome,
  not an error.

A process vanishing after a signal can never crash the monitor; the
/proc read simply fails and the message says so.

## Worked example

```
1. user selects  bash (302)     in the table
2. [C]ontrol -> 1 Pause
   ctrl_validate: not init/self/ancestor      -> allowed
   kill(302, SIGSTOP)                          -> OK
   re-read: state is T                         -> "paused - state STOPPED (T)"
   table refresh shows STOPPED (T)
3. [C]ontrol -> 2 Resume
   kill(302, SIGCONT)                          -> OK
   re-read: state is S                         -> "resumed - state SLEEPING (S)"
4. [C]ontrol -> 4 Force kill -> y
   kill(302, SIGKILL)                          -> OK
   re-read: ENOENT                             -> "no longer running"
```

All numbers in the example are illustrative; the program itself only
ever shows PIDs, names and states read from the live system.
