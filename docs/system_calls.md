# Linux / POSIX APIs and System Calls Used

Every mechanism below is used directly by the application. None of the
functionality shells out to `ps`, `top`, `kill` or any other utility.

## Process identification and state

| API / file | Where used | Purpose |
|---|---|---|
| `getpid()`, `getppid()` | process_control, process_demo | Identify self, refuse to signal self/parent, demo PIDs |
| `/proc/<PID>/stat` | process_monitor | pid, ppid, comm, state letter, utime/stime, priority, nice, threads, rss |
| `/proc/<PID>/status` | process_monitor | `Uid:` line for the owning user (details view also reads VmRSS-related lines) |
| `/proc/<PID>/cmdline` | process_monitor | Full command line (kernel stores args NUL-separated); empty for kernel threads, shown as `[comm]` |
| `/proc/<PID>/exe` | process_monitor | `readlink()` target = executable path |

## System information

| API / file | Where used | Purpose |
|---|---|---|
| `uname()` | system_info | Hostname, kernel name and release |
| `/proc/uptime` | system_info | System uptime in seconds |
| `/proc/loadavg` | system_info | 1/5/15-minute load averages |
| `/proc/meminfo` | system_info | MemTotal, MemAvailable (RAM usage) |
| `/proc/stat` | system_info | Aggregate CPU tick counters for interval utilization |
| `sysconf(_SC_CLK_TCK)` | system_info | Clock ticks per second used by all CPU math |
| `sysconf(_SC_NPROCESSORS_ONLN)` | system_info | Online CPU count |
| `sysconf(_SC_PAGESIZE)` | system_info | Page size for RSS -> MB conversion |

## Process control (signals)

| API | Where used | Purpose |
|---|---|---|
| `kill(pid, SIGSTOP)` | process_control | Pause a selected process |
| `kill(pid, SIGCONT)` | process_control | Resume a stopped process |
| `kill(pid, SIGTERM)` | process_control | Politely request termination |
| `kill(pid, SIGKILL)` | process_control | Force termination (confirmation required) |
| `errno` (`ESRCH`, `EPERM`, `EINVAL`) | process_control | Map failures to clear terminal messages |

The safety layer (`ctrl_validate`) refuses PID 1, the monitor itself
and any of its ancestors before any signal is sent. After every
signal the process is re-read from /proc to verify the actual effect.

## Process creation demonstration

| API | Where used | Purpose |
|---|---|---|
| `fork()` | process_demo | Duplicate the process; child gets PID 0 return |
| `execl("/bin/echo", ...)` | process_demo | Replace the child's image with a new program |
| `waitpid(pid, &status, 0)` | process_demo | Parent synchronizes with the child |
| `WIFEXITED`, `WEXITSTATUS`, `WIFSIGNALED`, `WTERMSIG` | process_demo, tests | Interpret the real termination status |

The demo also illustrates a classic `fork()` pitfall: `stdout` is
flushed *before* `fork()`, because a child inherits a copy of the
parent's stdio buffer — forgetting this prints buffered output twice.

## Scanning and I/O infrastructure

| API | Where used | Purpose |
|---|---|---|
| `opendir()` / `readdir()` / `closedir()` | process_monitor | Enumerate numeric PID directories under /proc |
| `fopen()` / `fread()` / `fclose()` | utils | Bounded reads of /proc pseudo-files |
| `readlink()` | process_monitor | Resolve `/proc/<PID>/exe` |
| `getpwuid()` | process_monitor | UID -> account name |
| `strtoul`/`strtol` with full-string checks | utils, process_monitor | Strict numeric parsing of kernel fields |
| `getpwuid_r()` | utils | UID -> account name (thread-safe lookup with cache) |
| `poll()` | terminal_ui | Input with timeout (drives auto-refresh without busy-waiting) |
| `tcgetattr()` / `tcsetattr()` | terminal_ui | Raw terminal mode; restored on every exit path |
| `ioctl(TIOCGWINSZ)` | terminal_ui | Terminal width/height, re-read every frame for live resize |
| `sigaction()` | terminal_ui | SIGINT/SIGTERM/SIGHUP raise a stop flag; terminal is restored before exit |
| `qsort()` | process_monitor | Deterministic sorting (PID tie-break) |
