# System Architecture

## Overview

The Linux Process Monitoring and Control System is a terminal application
written in C11 using only the C standard library and Linux/POSIX APIs.
It reads every displayed value directly from the kernel through `/proc`
and syscalls — nothing is hardcoded or faked.

```
                +------------------------------------------+
                |                 main.c                   |
                |   CLI parsing, app lifecycle, dispatch   |
                +---------------------+--------------------+
                                      |
                     +----------------+------------------+
                     |          terminal_ui.c            |
                     |  raw mode, ANSI rendering, input  |
                     +--+-----------+-----------+--------+
                        |           |           |
          +-------------+    +------+     +-----+--------+
          v                  v                 v
   process_monitor.c   process_control.c  process_tree.c
   /proc scan+parse    kill()+signals     PID/PPID graph
   interval CPU%       safety+verify      tree rendering
          |                  |                 |
          +--------+---------+---------+-------+
                   v                   v
            system_info.c         process_demo.c
            uname, meminfo        fork/exec/waitpid
            /proc/stat            (isolated screen)
                   |
                   v
             Linux kernel (/proc, syscalls)
```

## Module responsibilities

| Module | Responsibility |
|---|---|
| `main.c` | CLI entry point (`--once`, `--tree`, `--demo`, `--help`), starts the interactive app |
| `process_monitor.c/h` | /proc discovery, stat/status/cmdline parsing, snapshot store, interval CPU engine, sort/search/filter |
| `process_control.c/h` | SIGSTOP/SIGCONT/SIGTERM/SIGKILL delivery through `kill()`, safety policy, post-signal verification |
| `process_tree.c/h` | Parent/child index from PPID, deterministic ordering, ASCII/Unicode tree rendering |
| `process_demo.c/h` | Educational fork() -> execl() -> waitpid() demonstration (separate from monitoring) |
| `system_info.c/h` | Hostname, kernel, uptime, load average, memory, system-wide interval CPU |
| `terminal_ui.c/h` | termios raw mode, ANSI escape sequences, single `write()` per frame, keyboard handling, popups, batch printers |
| `utils.c/h` | Safe string ops, UTF-8-aware truncation, file readers, uid lookup with cache, uptime formatting |

## Core monitoring data flow

```
/proc (numeric dirs)
  -> get_process_info(pid)
       -> /proc/<PID>/stat     pid ppid comm state CPU ticks starttime threads rss
       -> /proc/<PID>/cmdline  full command line (args NUL-separated)
       -> /proc/<PID>/exe      executable path (readlink)
  -> ProcessInfo (heap snapshot)
  -> monitor_refresh(): interval CPU% vs previous scan + system_info_read_dynamic()
  -> terminal_ui renders the snapshot
```

A full scan runs every second in the interactive view. Processes that exit
mid-scan are simply skipped (their /proc entries vanish), so a racing exit
can never crash or corrupt a refresh.

## Interval CPU measurement

CPU percentages are never lifetime averages. Each refresh:

1. Reads the aggregate system tick counters from `/proc/stat`
   (`read_system_cpu`).
2. Reads every process's `utime + stime` from `/proc/<PID>/stat`.
3. Looks the process up in the previous scan's tick table
   (`MonitorState` — an open-addressing hash keyed by PID and start time).
4. `cpu% = 100 * (process_ticks_now - process_ticks_prev) /
            (system_ticks_now - system_ticks_prev) * online_CPU_count`

Process ticks and system ticks come from the same kernel interval and
are counted in the same unit (`CLK_TCK`), so no wall-clock timing is
needed — the same multicore convention `top` uses. Tracking start time
prevents a reused PID from inheriting another process's counters. A newly discovered process shows
0.0% until its second sample exists; entries for processes that
exited are pruned after each scan. The same engine serves the table,
the details view (via `monitor_sample_details`' rolling baseline) and
batch mode.

## Terminal rendering

The UI never prints pages of text. Each frame is composed into an
in-memory buffer (`FrameBuf`) inside the alternate screen buffer
(`ESC[?1049h`) with the cursor hidden. The first frame is written in
full; after that, the flush compares each row's plain text against
the previous frame and emits **only the rows that changed** (a few
dozens of bytes per second on a quiet system). This eliminates the
full-screen repaint flicker that conpty/Windows Terminal shows for
every rewrite. `poll()` on stdin with a 1000 ms timeout drives both
input and the refresh cadence; the terminal size is re-read with
`ioctl(TIOCGWINSZ)` every frame, so resizes are picked up without a
dedicated signal path. `SIGINT`, `SIGTERM` and `SIGHUP` only raise a
stop flag (async-signal-safe); the main loop then restores the
original terminal mode before exiting — including on Ctrl-C.

Input uses a decode queue: one `read()` may return several keys at
once (held arrow key, pasted text); every byte is queued and
processed, so fast key presses are never dropped, and the cursor
position doubles as the selection identity so the auto-refresh never
snaps the highlight back.

## Error handling strategy

- Every `/proc` read failure (ENOENT, EACCES, malformed) is contained:
  the process is skipped or an error message is produced, never a crash.
- Before `kill()`, the selected PID's start time is re-read to reject a
  vanished or reused PID; results map ESRCH/EPERM/EINVAL to messages.
- Allocation failures degrade gracefully (rendering skips, scans abort
  with an error string, tests fail loudly).
- Any process referenced by the UI (details, control) is re-read from
  /proc after each action; disappearance is reported, never fatal.
