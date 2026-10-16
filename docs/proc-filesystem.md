# The /proc Filesystem in This Project

`/proc` is a pseudo-filesystem: the kernel synthesizes its files at read
time, so every read returns the *current* state of the system. This
project treats /proc as its only process-data source.

## Layout used

```
/proc
├── <PID>/                 one numeric directory per process
│   ├── stat               compact, machine-readable status (always readable)
│   ├── status             human-readable status incl. Uid (may need perms)
│   ├── cmdline            argv of the process (NUL-separated, may be empty)
│   └── exe                symlink to the running executable
├── stat                   aggregate CPU counters ("cpu  ...")
├── meminfo                MemTotal / MemAvailable / ...
├── loadavg                1, 5, 15-minute load averages
└── uptime                 seconds since boot
```

## /proc/<PID>/stat parsing

The stat format is one line with whitespace-separated fields — except
field 2, `(comm)`, which is parenthesized and may itself contain spaces
and parentheses (e.g. `Relay(302)` or a browser with a long title).

Parsing strategy (`parse_stat_line`):

1. Slice the PID before the first `(`, trimming the separator space.
2. Take the process name between the *first* `(` and the *last* `)`.
3. Scan the remainder with one strict `sscanf`: state (3), ppid (4),
   then skip pgrp..cmajflt, read utime (14), stime (15), skip
   cutime/cstime, read priority (18), nice (19), num_threads (20),
   skip itrealvalue, read starttime (22), skip vsize, and read rss (24)
   in pages. Start time distinguishes reused PIDs.
4. A line that does not yield every required field is rejected —
   the process is skipped, never guessed at.

A line with fewer than the required fields is rejected as malformed and
the process is skipped — never guessed at.

## State letters

| Code | Meaning shown in the UI |
|---|---|
| `R` | RUNNING |
| `S` | SLEEPING |
| `D` | DISK SLEEP (uninterruptible, usually disk I/O) |
| `T` / `t` | STOPPED (SIGSTOP'd) / TRACING (debugged) |
| `Z` | ZOMBIE (exited, not yet reaped) |
| `I` | IDLE (kernel worker) |
| `P` | PARKED (per-CPU kernel worker) |
| `X` | DEAD |

The UI shows both the letter and the name, e.g. `RUNNING (R)`.

## Command line vs kernel threads

`cmdline` is empty for kernel threads. Following `ps` convention, the
application displays them as `[kthread_name]`. For normal processes the
args are joined with single spaces for display.

## Races: processes appear and disappear

A scan of /proc is inherently racy — a process listed by `readdir()`
may exit before we read its stat file. The application therefore:

- ignores any per-process read failure (`ENOENT` most commonly, also
  `EACCES` for other users' processes under hidepid) and continues;
- validates that the PID reported inside stat matches the directory
  name we read from;
- only reports an error when /proc itself cannot be opened.

The same applies after signals: a killed process simply disappears,
which the control layer reports as "no longer running".

## Why not lifetime CPU?

`utime + stime` in stat are *cumulative* since process start. Dividing
by process uptime (the `top`-style lifetime share) hides bursts. This
project instead stores the previous scan's counters and divides the
*delta* by the elapsed wall-clock interval, which is what a user
expects a live percentage to mean.
