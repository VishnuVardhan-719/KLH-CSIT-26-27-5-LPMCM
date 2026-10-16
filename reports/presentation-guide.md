# Presentation & Viva Guide — Linux Process Monitoring and Control System

Section 12 · Team 05 · Academic Year 2026–27 · Branch CSIT · Repo `KLH-CSIT-26-27-5-LPMCM`

| Roll Number | Member | Responsibility area |
|---|---|---|
| 2520090166 | R. Vishnu Vardhan | /proc discovery & parsing, interval CPU sampling, system info |
| 2520090070 | K. Amruthavalli | Process control: kill()/signals, safety policy, verification |
| 2520090031 | N. Divya Sree | fork/exec/waitpid demo, test suite, documentation |

---

## 1. One-line definition (memorize this)

> "A terminal-based Task Manager for Linux, written in C, that reads every
> process's data directly from the kernel's /proc filesystem, measures CPU
> with true interval sampling, and controls processes with POSIX signals —
> implemented by us, not by shelling out to ps/top/kill."

---

## 2. The problem we solved

Linux exposes process information only through scattered tools — `ps`,
`top`, `kill`, `pstree` — each a separate program. As OS students we
wanted one coherent tool that *implements* the mechanisms itself:

- discovers every running process automatically (no PID typing),
- shows real CPU%, memory, state, user, parent for each,
- renders the true parent/child hierarchy,
- pauses/resumes/kills a selected process via signals,
- demonstrates fork/exec/waitpid live.

## 3. What we built (feature list)

| Feature | Where in the app |
|---|---|
| Live process table (PID, name, user, CPU%, MEM%, state, PPID) | main screen, 1 s refresh |
| Sort by PID/name/CPU/memory/state/user | `S` key |
| Filter by state (R/S/D/T/Z/I) | `F` key |
| Search by name/user/PID (case-insensitive) | `/` key |
| Process details (exe path, cmdline, threads, nice, priority) | Enter |
| Process tree from PID/PPID | `T` key |
| Pause / Resume / Stop / Force-kill | `C` popup or details view |
| SIGKILL confirmation prompt (y/N, default No) | before every kill |
| Safety: refuses PID 1, itself, and its ancestors | always on |
| fork/exec/waitpid demonstration | `D` key |
| System header: hostname, kernel, uptime, CPU%, RAM | top of screen |
| Batch modes for evidence: `--once`, `--tree`, `--demo` | command line |

## 4. Technology stack (and why)

| Layer | Choice | Why |
|---|---|---|
| Language | C11 | The OS concepts (fork, signals, /proc) are C-native; no runtime hides the syscalls |
| Platform | Ubuntu 24.04 (WSL2 for dev) | /proc is a Linux-only interface |
| Compiler | GCC 13.3 with `-Wall -Wextra -std=c11` | zero-warning policy |
| Build | GNU Make | one-command build/test/run |
| Libraries | C stdlib + POSIX only | no ncurses, no external deps — everything hand-built |
| UI | termios raw mode + ANSI escapes + UTF-8 box art | professional TUI without ncurses |
| Testing | 104 C checks + 2 CLI checks + ASan/UBSan | correctness and memory-safety evidence |

## 5. Architecture (draw this on the whiteboard)

```
                main.c  (CLI: --once --tree --demo --help / TUI)
                   |
            terminal_ui.c  (raw mode, diff rendering, keys, popups)
             |      |      |
   process_monitor.c  process_control.c  process_tree.c
   /proc scan+parse   kill()+safety      PID/PPID graph
   interval CPU%      + verification
             \      |      /
          system_info.c     process_demo.c
          uname/meminfo     fork/exec/waitpid
          /proc/stat        (isolated screen)
                   |
             utils.c  (safe strings, UTF-8, uid cache, file reads)
                   |
            Linux kernel: /proc, syscalls
```

Rule we enforced: **/proc is parsed in exactly one module**
(process_monitor.c). Every other view consumes its data — no duplicated
parsing anywhere.

## 6. The core OS concepts (this is what the examiners want)

### 6.1 The /proc filesystem
A pseudo-filesystem the kernel synthesizes at read time. Each process has
`/proc/<PID>/`:
- `stat` — machine-readable: pid, comm (in parens, may contain spaces!),
  state letter, ppid, utime/stime (CPU ticks), priority, nice, threads, RSS
- `cmdline` — argv (NUL-separated; empty for kernel threads → we show `[name]`)
- `exe` — symlink to the binary (readlink)
- plus global files: `/proc/stat` (CPU jiffies), `meminfo`, `loadavg`, `uptime`

Parsing gotcha we handled: comm can contain spaces/parens
(`Relay(302)`), so we slice from the first `(` to the **last** `)` and
trim the separator space before the PID.

### 6.2 Interval CPU measurement (not lifetime average)
`top`-style delta sampling, exactly what we implemented:

```
snapshot1: process utime+stime, system total jiffies  (t0)
        wait 1 s
snapshot2: same counters                              (t1)

CPU% = 100 × process_tick_delta / system_tick_delta × online_CPU_count
```

Both counters come from the same kernel window and the same unit
(CLK_TCK), so no wall-clock division is even needed. First sample of a
new process = 0.0% (no baseline yet). We store previous ticks in an
open-addressing hash keyed by PID plus process start time: O(1) lookup,
safe PID-reuse handling, and pruning of exited processes after each scan.

### 6.3 Memory accounting
RSS pages (from stat) × page size (`sysconf(_SC_PAGESIZE)`) = bytes →
MB; percentage against `MemTotal` from `/proc/meminfo`.

### 6.4 Process states
Kernel letter → human name: R=RUNNING, S=SLEEPING, D=DISK SLEEP,
T=STOPPED, Z=ZOMBIE, I=IDLE. Shown as `SLEEPING (S)`.

### 6.5 Signals & kill()
| Action | Signal | Recoverable? |
|---|---|---|
| Pause | SIGSTOP | yes (SIGCONT) |
| Resume | SIGCONT | — |
| Stop | SIGTERM | process may clean up / ignore |
| Kill | SIGKILL | **no** → hence the y/N confirmation |

We call `kill(pid, sig)` directly. Failure mapping: ESRCH → "no longer
exists", EPERM → "owned by another user", EINVAL → internal error.

### 6.6 Safety policy (before ANY signal)
1. `pid <= 1` refused (init/systemd),
2. own PID refused,
3. entire ancestor chain (walked via PPID in /proc) refused.
Verified live: selecting PID 1 and pressing Pause shows
`PID 1 is protected (init/system)` and sends nothing.

### 6.7 fork / exec / waitpid — the demo
```
parent: pid = fork()          → two identical processes; child gets 0
child : execl("/bin/echo",…)  → address space replaced, same PID
parent: waitpid(pid,&st,0)    → blocks until child exits; WIFEXITED,
                                WEXITSTATUS give the real status
```
We also flush stdout **before** fork() — the classic pitfall: the child
inherits a *copy* of the parent's stdio buffer; forgetting this prints
everything twice.

### 6.8 Process tree
Built from real PPID links: children indexed per parent, sorted by PID,
roots = PID 1 + orphans (parent exited between scans) + ppid==0.
Cycle guard + depth cap make malformed data impossible to hang on.

## 7. Engineering highlights (mention these to score points)

1. **Flicker-free rendering** — frames are composed in memory; after the
   first full paint we transmit **only changed rows** (~18 bytes/s idle
   vs ~16 KB before). This fixed real jitter on Windows Terminal/conpty.
2. **Input queue** — one `read()` can return several keys (held arrow
   key); all bytes are queued and decoded, so fast presses are never
   dropped. Cursor position *is* the selection identity, so the 1 s
   auto-refresh can never snap the highlight back.
3. **Race tolerance** — processes vanish constantly; every /proc read
   treats ENOENT/EACCES as "skip this round". The monitor cannot crash
   because a process died mid-scan.
4. **Memory safety** — 104 C checks, 2 CLI checks and ASan/UBSan with
   leak detection: zero findings. Bounded copies everywhere
   (`sstrcpy`, `utf8_copy`), no fixed 4096-entry stack arrays (heap
   ProcVec instead).
5. **Zero-warning build** with `-Wall -Wextra -std=c11`.

## 8. Testing story

- `make test` — 104 C checks in 16 groups plus 2 CLI checks: parsing
  (synthetic stat lines
  with spaces in comm), CPU math (20/100=20%, backwards counters→0),
  live /proc discovery (finds PID 1 and itself), vanished-PID handling,
  safety gates, and **real signal round-trips on disposable forked
  children** (SIGSTOP→state T observed, SIGCONT, SIGTERM→WIFSIGNALED,
  SIGKILL→WTERMSIG).
- `make sanitize` — same suite under AddressSanitizer+UBSan with leak
  detection: zero findings.
- Evidence files in `results/`: build log (0 warnings), table, tree,
  demo transcript, test log, pty TUI capture, and the PID-1-refusal
  screenshot-in-text.

## 9. Demo script (5 minutes, rehearse this order)

1. `make clean && make` — point at **zero warnings**.
2. `./build/lpmcs --once` — real table, real header values.
3. `make run` — live TUI; let it refresh twice (CPU% appears on 2nd).
4. Arrows to move; `S` → sort by CPU; `/firefox` search demo.
5. `T` — tree; show bash under SessionLeader under systemd.
6. In another terminal: `sleep 300 &` → find it (search "sleep") →
   `C` → **1 Pause** → state becomes STOPPED (T) → **2 Resume** →
   SLEEPING again → **3 Stop** → "no longer running".
7. `C` → **4 Kill** on some safe process → show the y/N prompt →
   answer N first, then demonstrate a real kill on the sleep process.
8. Try Pause on **PID 1** → refusal message. Safety proven live.
9. `D` — fork/exec/waitpid demo with real PIDs and exit status 0.
10. `q` — terminal restored perfectly (alternate screen + cursor back).

## 10. Likely viva questions & crisp answers

**Q: Why not just call ps/top?**
A: The spec forbids it and it defeats the purpose — those are finished
tools; we implement the mechanisms ourselves from /proc and syscalls.

**Q: Why is CPU% 0.0 the first time?**
A: Interval measurement needs two samples; the first establishes the
baseline. (top behaves the same way.)

**Q: Can CPU% exceed 100?**
A: Yes, up to nproc×100% for multi-threaded processes — our formula
divides by *system-wide* jiffies, so a 4-thread process on 4 cores can
legitimately show 400%.

**Q: What if a process dies between readdir and reading its stat?**
A: The read fails with ENOENT; we skip that process for the round.
Tested explicitly ("disappearing process" group).

**Q: Why SIGKILL confirmation but not SIGTERM?**
A: SIGKILL is uncatchable and unrecoverable; SIGTERM lets the process
clean up and can be re-run. Confirmation only guards irreversible harm.

**Q: How do you know SIGSTOP worked?**
A: After every signal we re-read /proc/<PID>/stat and report the
observed state — "paused - state STOPPED (T)" — never an assumption.

**Q: Difference between SIGTERM and SIGKILL?**
A: TERM can be caught/ignored/handled (graceful shutdown); KILL is
enforced by the kernel scheduler, the process never even sees it.

**Q: Zombie vs orphan?**
A: Zombie = exited but not yet reaped by its parent (state Z, still in
the table); orphan = parent died, reparented to init/subreaper — our
tree attaches orphans as roots.

**Q: fork vs exec?**
A: fork duplicates the calling process (two returns, same program);
exec *replaces* the current image with a new program (same PID). Real
shells combine them: fork, then exec the command.

**Q: waitpid vs wait?**
A: wait() blocks for *any* child; waitpid() targets a specific PID and
supports WNOHANG — we need the specific child's status.

**Q: What is /proc/stat's first line?**
A: Aggregate CPU jiffies across all cores: user nice system idle
iowait irq softirq steal — we use total and idle+iowait for the
system-wide percentage.

**Q: Why an open-addressing hash for previous ticks?**
A: O(1) lookup by PID plus start time on every refresh, automatic
pruning of dead processes by generation counter, and power-of-two
sizing with Knuth's multiplicative hash.

**Q: Raw mode — what does it change?**
A: Disables ICANON (keys arrive immediately, no line buffer) and ECHO;
we deliberately keep ISIG so Ctrl-C still generates SIGINT — which our
handler converts into a clean shutdown with terminal restore.

**Q: Alternate screen?**
A: `ESC[?1049h` switches to a scratch buffer; on exit `ESC[?1049l`
restores the user's scrollback exactly — the app leaves no trace.

**Q: Biggest bug you fixed?**
A: The kernel's stat format puts a space between PID and (comm); our
first parser rejected every real line — the unit tests caught it
immediately. Also: an initial zero-capacity hash table caused an
infinite probe loop on the first scan.

**Q: Limitations?**
A: Other users' processes are listed but not controllable without
root (EPERM — reported cleanly); exe/cmdline may be unreadable for
setuid/kernel threads; per-process CPU% needs one refresh warm-up;
UI assumes an ANSI terminal (falls back to ASCII art).

## 11. Repository map (for the "show your structure" slide)

```
KLH-CSIT-26-27-5-LPMCM/
├── README.md            full project document
├── Makefile             make / run / test / sanitize / clean
├── src/                 8 modules + headers (~2,900 lines C)
├── tests/               104 C checks + 2 CLI checks
├── docs/                architecture, system_calls, proc-filesystem,
│                        process-control, testing
├── results/             evidence: build/test/tree/demo/TUI captures
├── reports/             this guide, phase reports
└── data/                reference material
```

## 12. Numbers to quote

- ~3,000 lines of C across 8 modules plus the test suite
- 104 C checks + 2 CLI checks; ASan/UBSan clean
- 0 compiler warnings (-Wall -Wextra -std=c11, GCC 13.3)
- 1 s refresh; ~18 bytes/s terminal output when idle (diff rendering)
- 4 signals, PID-identity verification, 3 hard safety gates, 16 test groups
- Ubuntu 24.04, kernel 6.18 (WSL2), GCC 13.3, GNU Make 4.3

## 13. Division of work (say it as implemented modules)

- **Vishnu** — process_monitor.c (discovery, stat/status/cmdline/exe
  parsing, interval CPU engine, tick hash), system_info.c
- **Amruthavalli** — process_control.c (kill, safety policy, errno
  mapping, post-signal verification), safety UX (confirmation,
  refusal messages)
- **Divya** — process_demo.c (fork/exec/waitpid), tests/run_tests.c
  (104 checks), CLI checks, docs/ and results/ evidence

Each member should be ready to explain their own module's code on
screen — that is the first thing panels check.
