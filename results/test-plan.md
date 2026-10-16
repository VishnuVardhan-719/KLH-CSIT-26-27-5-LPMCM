# Test Plan — Linux Process Monitoring and Control System

## Automated suite (`make test`, 104 C checks + 2 CLI checks)

| # | Group | What is verified |
|---|---|---|
| A1 | utils | safe string copy/truncate, trim, case-insensitive search, uptime format, UTF-8 width |
| A2 | system_info | hostname/kernel/CPU/MemTotal read from the live machine; meminfo parsing incl. prefix-key confusion |
| A3 | stat parsing | synthetic `/proc/<PID>/stat` lines with spaces in comm, including field 22 starttime identity |
| A4 | malformed stat | empty, unbalanced parens, non-numeric pid, truncated lines rejected without crash |
| A5 | status parsing | Uid (first tab field), VmRSS kept intact, missing key and NULL buffer rejected |
| A6 | CPU interval math | 20/100 = 20%, 50/1000 = 5%, zero system delta -> 0, backwards counters -> 0, all zeros -> 0 |
| A7 | states & search | R/S/D/T/Z/I naming with UNKNOWN fallback; search by name/user/PID case-insensitively; state filter |
| A8 | live /proc | monitor discovers PID 1 and itself with valid state/user; plausible count; second refresh reuses the buffer |
| A9 | vanished PIDs | negative and nonexistent PIDs fail cleanly (ENOENT path); an exec'd-and-exited child reads as gone, no crash |
| A10 | safety gates | PID 1/0/negative, the monitor itself and its ancestors refused with explanatory messages |
| A11 | signal lifecycle | SIGSTOP -> state T observed in /proc; SIGCONT; SIGTERM -> reaped with WIFSIGNALED; dead PID -> clean ESRCH; SIGKILL -> WTERMSIG == SIGKILL |
| A12 | fork/exec/waitpid | child execs /bin/true, parent waitpid gets exit status 0 |
| A13 | tree building | forked child appears in the tree built from the live snapshot; deterministic root |
| A14 | top-style CPU | a real busy thread reports near 100% on a multicore machine |
| A15 | CPU history pruning | deliberate hash collision survives deletion of an earlier entry |
| A16 | malformed tree cycle | each PID is rendered exactly once |
| A17 | CLI integration | `--once` takes two samples and reports non-zero CPU for a busy process |
| A18 | fork output | redirected test output contains one header, not a duplicated stdio buffer |

Run: `make test` (expect both end-to-end checks to print `ok`)
Memory safety: `make sanitize` (AddressSanitizer + UBSan with leak detection, zero findings).
Destructive signals target only disposable children the suite forks itself.

## Manual test scenarios (interactive TUI)

| # | Action | Expected |
|---|---|---|
| M1 | `./build/lpmcs` | live table, ~1 s refresh, header shows real host/kernel/RAM/CPU |
| M2 | ↑/↓ | selection moves; row highlighted |
| M3 | Enter on a row | details: exe, cmdline, threads, nice, `STATE (X)` |
| M4 | `s`, `f`, `/` | sort popup, state filter popup, inline search; Esc cancels |
| M5 | `t` | tree of the real hierarchy, scrollable, B returns |
| M6 | `d` | fork/exec/waitpid demo prints once, correct PIDs and status |
| M7 | `sleep 300 &` in a shell; Pause it via [C] | row state becomes STOPPED (T) |
| M8 | Resume that process | state returns to SLEEPING (S) |
| M9 | Stop that process | row disappears; status "no longer running" |
| M10 | Try to control a root process as non-root | "permission denied (owned by another user)", no crash |
| M11 | Attempt SIGKILL | confirmation popup, default No; Yes kills after confirm |
| M12 | `q` | terminal restored (cursor visible, normal buffer) |## Environment evidence

Recorded on Ubuntu 24.04 (WSL2), GCC 13.3, GNU Make 4.3,
kernel 6.18.33.2-microsoft-standard-WSL2.

- `make` — zero warnings with `-Wall -Wextra -std=c11 -O2`
- `make test` — 104 C checks plus 2 CLI checks
- `make sanitize` — 104 checks, no ASan/UBSan findings or leaks
- `--once` / `--tree` / `--demo` outputs captured during development;
  the TUI was exercised in a pseudo-tty (raw mode, frame drawing, 'q'
  restores the terminal: `ESC[?1049l ESC[?25h`).
