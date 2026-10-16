# Operating Systems & System Programming (OSSP) — Course Repository & Project LPMCM

## Course & Student Information
- **Course:** Operating Systems & System Programming (OSSP)
- **Primary Student / Roll Number:** `2520090166` — R. Vishnu Vardhan
- **Academic Year:** 2026–2027 (2nd Year Odd Semester)
- **Institution:** KL University Hyderabad (KLH)

---

## Section / Team Details
- **Section No.:** 12
- **Team No.:** 05
- **Project Title:** Linux Process Monitoring and Control System (LPMCM - Terminal Edition)
- **Repository:** `KLH-CSIT-26-27-5-LPMCM` (branch `main`)

### Team Members
| Roll Number | Student Name | Area / Responsibility |
|---|---|---|
| 2520090166 | R. Vishnu Vardhan | Architecture, /proc scanning & parsing, CPU interval engine, system_info |
| 2520090070 | K. Amruthavalli | Process control module: signals, `kill()`, safety policy, error mapping |
| 2520090031 | N. Divya Sree | Process tree graph, fork/exec/waitpid demo, test suite, documentation |

### Current Phase
Phases 1–10 Complete: Build system, /proc discovery, process parsing, CPU/memory monitoring, live terminal UI, process control with safety policy, process tree, fork/exec/waitpid demo, 104 C test checks, AddressSanitizer/UBSan validation, and documentation.

---

## Repository Structure

```text
KLH-CSIT-26-27-5-LPMCM/
│
├── 2520090031/                         # Coursework: N. Divya Sree
│   ├── Practical/                      # Weekly practical assignments & lab tasks
│   └── Skill/                          # Weekly skill exercises
│
├── 2520090070/                         # Coursework: K. Amruthavalli
│   ├── Practical/                      # Weekly practical assignments & lab tasks
│   └── Skill/                          # Weekly skill exercises
│
├── 2520090166/                         # Coursework: R. Vishnu Vardhan
│   ├── Practical/
│   │   ├── Practical-01/               # Linux setup & WSL installation
│   │   ├── Practical-02/               # Process management & creation
│   │   ├── Practical-03/               # CPU scheduling & synchronization
│   │   └── Practical-04/               # Memory management & syscalls
│   └── Skill/
│       ├── Skill-01/                   # Basic terminal navigation & permissions
│       ├── Skill-02/                   # Shell scripting & text processing
│       ├── Skill-03/                   # Inter-Process Communication (IPC)
│       └── Skill-04/                   # POSIX signals & multithreading
│
├── ForgeOS/                            # Systems programming module
│   ├── Shellforge/                     # UNIX shell implementation
│   └── NanoKernel/                     # Minimal kernel primitives
│
├── src/                                # LPMCS Core Application (8 C modules)
│   ├── main.c                          # CLI parsing, app lifecycle, dispatch
│   ├── process_monitor.c/h             # /proc discovery & interval CPU engine
│   ├── process_control.c/h             # kill() signals & safety gates
│   ├── process_tree.c/h                # PID/PPID hierarchy graph
│   ├── process_demo.c/h                # fork() -> exec() -> waitpid() demo
│   ├── system_info.c/h                 # uname, uptime, meminfo, loadavg
│   ├── terminal_ui.c/h                 # Raw-mode ANSI interactive UI
│   └── utils.c/h                       # String formatting & safe readers
│
├── docs/                               # System architecture & API specifications
│   ├── architecture.md
│   ├── proc-filesystem.md
│   ├── process-control.md
│   ├── system_calls.md
│   └── testing.md
│
├── tests/                              # Automated test suite (104 C checks + 2 CLI checks)
├── results/                            # Test plan & verification evidence logs
├── reports/                            # Phase reports & presentation guide
├── data/                               # Reference datasets
├── Makefile                            # Automated build & test orchestration
├── .gitignore                          # Git tracking exclusions
└── README.md                           # Master documentation
```

---


## Course Project: Linux Process Monitoring and Control System (LPMCS)

### 1. Abstract
The Linux Process Monitoring and Control System is a terminal application written in C11 that observes and manages active Linux processes. The application continuously discovers processes by scanning the `/proc` filesystem, parses kernel data structures directly, and presents a live, keyboard-driven dashboard with per-process CPU, memory, state, user, and hierarchy information. Users select processes with arrow keys and control them using POSIX signals (`SIGSTOP`, `SIGCONT`, `SIGTERM`, `SIGKILL`) through a safety layer that protects system-critical processes. A separate educational module demonstrates `fork()`, `exec()`, and `waitpid()` working together. Every value displayed is read live from the Linux kernel; nothing is simulated.

### 2. Problem Statement
Linux exposes process information through many separate tools (`ps`, `top`, `kill`, `pstree`), which makes it difficult to study Operating Systems process concepts as a coherent whole. None of these tools are designed to demonstrate how the underlying mechanisms interact. This project replaces those separate interfaces with a single terminal system that implements monitoring and control itself on top of `/proc` and POSIX system calls, making each OS concept (PIDs, states, parent-child relations, scheduling counters, signals, process creation) visible and interactive.

### 3. Architecture
```text
main.c ── terminal_ui.c ──┬── process_monitor.c ── /proc scan + parse
                          ├── process_control.c ── kill() + safety policy
                          ├── process_tree.c ────── PID/PPID hierarchy graph
                          ├── process_demo.c ────── fork/exec/waitpid demo
                          └── system_info.c ─────── uname/meminfo/stat
                                     utils.c (shared helpers)
```

### 4. Linux / POSIX Technologies
| Concept / API | Used For |
|---|---|
| `/proc` filesystem (`stat`, `status`, `cmdline`, `exe`, `meminfo`, `stat`, `uptime`) | All process discovery, resource consumption, and system metrics |
| `kill()`, `SIGSTOP`, `SIGCONT`, `SIGTERM`, `SIGKILL` | Controlled process lifecycle management |
| `fork()`, `execl()`, `waitpid()` | Process creation and synchronization demonstration |
| `readdir()`, `readlink()`, `getpwuid_r()` | Dynamic `/proc` enumeration and user enrichment |
| `uname()`, `sysconf()` | Hardware architecture and page size resolution |
| `poll()` | Non-blocking input driving the 1-second refresh cadence |
| `tcgetattr()` / `tcsetattr()`, ANSI escapes | Flicker-free raw-mode terminal user interface |

### 5. Safety Policy & Integrity
- **PID 1 (init/systemd) Protection:** The monitor refuses to signal PID 1 to prevent system lockups.
- **Self & Ancestor Protection:** Traverses `/proc/<PID>/stat` PPID chain to prevent signaling the monitor or its parent shell.
- **Race-Free Signal Verification:** Checks process start time (`starttime`) before signal delivery to prevent signaling recycled PIDs (`ctrl_send_verified()`).
- **Confirmation Requirement:** `SIGKILL` requires explicit `[y/N]` confirmation before execution.

---

## Build & Execution Instructions

### Prerequisites
Ubuntu 22.04+ / Debian or WSL2 on Windows, GCC 13+, GNU Make.
```bash
sudo apt update && sudo apt install -y build-essential
```

### Build Commands
```bash
make            # Compile build/lpmcs (-Wall -Wextra -std=c11, zero warnings)
make test       # Run 104 C checks + 2 end-to-end integration checks
make sanitize   # Run test suite under AddressSanitizer + UndefinedBehaviorSanitizer
make run        # Build and start the interactive terminal monitor
make clean      # Clean build artifacts
```

### Execution Modes
```bash
./build/lpmcs          # Interactive live dashboard (TUI)
./build/lpmcs --once   # One-shot table snapshot and exit
./build/lpmcs --tree   # Hierarchical PID/PPID tree display
./build/lpmcs --demo   # Fork / exec / waitpid execution lifecycle demo
./build/lpmcs --help   # Command-line help and usage
```

### Interactive Keyboard Controls
- `[↑ / ↓]` Navigate & select process
- `[Enter]` Process details inspection view
- `[C]` Process Control menu (Pause, Resume, Stop, Force Kill)
- `[T]` Process hierarchy tree view
- `[D]` Educational `fork()` / `exec()` / `waitpid()` demonstration
- `[S]` Sort mode toggle (PID, CPU%, MEM%, Name)
- `[F]` State filter (All, Running, Sleeping, Stopped, Zombie)
- `[/]` Search by process name
- `[R]` Manual instant refresh
- `[Q]` Clean exit with terminal mode restoration
