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
- **Project Title:** Linux Process Monitoring and Control System (LPMCM)

### Team Members
| Roll Number | Student Name | Signature |
|---|---|---|
| 2520090166 | R. Vishnu Vardhan | |
| 2520090070 | K. Amruthavalli | |
| 2520090031 | N.Divya Sree | |

### Current Phase
Phase 1 — Project Setup and Planning

---

## Repository Structure

```text
KLH-CSIT-26-27-5-LPMCM/
│
├── 2520090166_Practical/
│   ├── Practical-01/
│   │   ├── PRACTICAL_WEEK_1_OSSP.pdf
│   │   └── WSL_INSTALLATION_Vishnu.pdf
│   ├── Practical-02/
│   │   └── WEEK-2_OSSP.pdf
│   ├── Practical-03/
│   │   └── PRACTICAL_WEEK3_OS_R_Vishnu_Vardhan_2520090166.pdf
│   └── Practical-04/
│       └── PRACTICAL_WEEK4_OS_R_Vishnu_Vardhan_2520090166.pdf
│
├── 2520090166_Skill/
│   ├── Skill-01/
│   │   └── SKILL-WEEK1_OS_SIMPLE_COMMANDS_R_Vishnu_Vardhan_2520090166.pdf
│   ├── Skill-02/
│   │   └── SKILL_WEEK-2_OS_R_Vishnu_Vardhan_2520090166.pdf
│   ├── Skill-03/
│   │   └── SKILL_WEEK-3_OS_R_Vishnu_Vardhan_2520090166.pdf
│   └── Skill-04/
│       └── SKILL_WEEK-4_OS_R_Vishnu_Vardhan_2520090166.pdf
│
├── ForgeOS/
│   ├── Shellforge/
│   └── NanoKernel/
│
├── .gitignore
└── README.md
```

---

## Coursework Overview

### 1. Practical Work (`2520090166_Practical/`)
- **Practical-01:**
  - `PRACTICAL_WEEK_1_OSSP.pdf`: Introduction to Linux environment, foundational POSIX tools, and system setup.
  - `WSL_INSTALLATION_Vishnu.pdf`: Windows Subsystem for Linux (WSL/Ubuntu) installation and configuration guide.
- **Practical-02:**
  - `WEEK-2_OSSP.pdf`: Process management fundamentals, process identifiers, and process creation (`fork()` / `exec()`).
- **Practical-03:**
  - `PRACTICAL_WEEK3_OS_R_Vishnu_Vardhan_2520090166.pdf`: CPU scheduling algorithms, process synchronization, and concurrency mechanisms.
- **Practical-04:**
  - `PRACTICAL_WEEK4_OS_R_Vishnu_Vardhan_2520090166.pdf`: Memory management concepts, page replacement, and system call implementations.

### 2. Skill Work (`2520090166_Skill/`)
- **Skill-01:**
  - `SKILL-WEEK1_OS_SIMPLE_COMMANDS_R_Vishnu_Vardhan_2520090166.pdf`: Practical mastery of basic Linux terminal commands, filesystem navigation, and permissions.
- **Skill-02:**
  - `SKILL_WEEK-2_OS_R_Vishnu_Vardhan_2520090166.pdf`: Shell scripting, filters, pipe redirection, and text processing utilities.
- **Skill-03:**
  - `SKILL_WEEK-3_OS_R_Vishnu_Vardhan_2520090166.pdf`: Inter-Process Communication (IPC) techniques, pipes, and shared memory structures.
- **Skill-04:**
  - `SKILL_WEEK-4_OS_R_Vishnu_Vardhan_2520090166.pdf`: Advanced POSIX signaling, multithreading, and system programming exercises.

---

## ForgeOS Project

`ForgeOS` represents the core systems-programming module containing:
- **`Shellforge/`**: A custom UNIX shell implementation built in C, providing command parsing, execution pipelines, background jobs, and built-in commands.
- **`NanoKernel/`**: An experimental minimal kernel component demonstrating core primitives of operating system design, task execution, and memory addressing.

---

## Course Project: Linux Process Monitoring and Control System (LPMCM)

### 1. Abstract
The Linux Process Monitoring and Control System is designed to provide a unified environment for observing and managing active Linux processes. Linux offers process information and control through multiple command-line utilities and system interfaces, which can be difficult to study together. This project brings these concepts into a single Linux-based system that displays process IDs, parent-child relationships, process states, and resource usage, while also supporting controlled process operations. The implementation uses Linux/POSIX concepts including the `/proc` filesystem, process IDs, signals, and `kill()` for process monitoring and control. A separate demonstration component uses `fork()`, `exec()`, and `wait()` to illustrate process creation and synchronization concepts. Process information will be collected and presented in a structured form, and signals will be used to observe changes in selected processes. The expected outcome is a working process monitoring and control system that demonstrates practical understanding of process monitoring, signaling, resource tracking, and Linux system programming.

### 2. Problem Statement
Linux provides process monitoring and control mechanisms through different commands, APIs, and system interfaces. Using these mechanisms separately makes it difficult to obtain a unified view of process states, PIDs, parent-child relationships, resource usage, and process-control operations. The project addresses this problem by developing a Linux-based system that integrates process monitoring and basic process control while demonstrating core Operating Systems and POSIX system-programming concepts.

### 3. Objectives
1. Monitor and display information about active Linux processes.
2. Observe process states, process IDs, parent-child relationships, and resource usage.
3. Provide controlled process-management operations using POSIX/Linux signals.
4. Demonstrate Linux/POSIX process creation, execution, and synchronization concepts in a separate demo component.

### 4. Proposed Methodology
The core system will be implemented in Linux using C and POSIX/Linux interfaces. It will collect process information from the `/proc` filesystem and standard system interfaces, organize the information, and display process IDs, states, relationships, and resource usage. Process-control operations will be performed by sending appropriate signals using `kill()`. A separate demonstration component will use `fork()`, `exec()`, and `wait()` to demonstrate process creation, execution, and synchronization. The system will then observe and report resulting process-state changes in a structured manner.

### 5. Operating Systems Concepts / Linux APIs Used
| OS Concept / Linux API / System Call | Purpose in the Project |
|---|---|
| `/proc` filesystem | Read process information such as status and resource details. |
| PID / PPID / Process states | Identify processes, parent-child relationships, and execution states. |
| POSIX/Linux signals & `kill()` | Send controlled signals to selected processes. |
| `fork()`, `exec()`, `wait()` | Demonstrate process creation, execution, termination, and synchronization in a separate demo component. |

### 6. Individual Contribution
| Roll Number | Student Name | Individual Responsibility |
|---|---|---|
| 2520090166 | R. Vishnu Vardhan | Architecture, process parsing, and signal control engine |
| 2520090070 | K. Amruthavalli | `/proc` interface reader and process state representation |
| 2520090031 | N.Divya Sree | Process tree relationship mapper and demonstration module |

### 7. Tools / Platforms / Software Used
| Tool / Platform / Software | Purpose |
|---|---|
| Linux / Ubuntu / WSL2 | Development and execution platform; provides `/proc` and Linux process interfaces. |
| C | System-programming language for implementing process monitoring and control. |
| GCC | Compile and build the C source code. |
| Git / GitHub | Version control, collaboration, and project repository management. |

### 8. Expected Outcome
A working Linux-based Process Monitoring and Control System that displays active-process information, process states, resource usage, and parent-child relationships, and performs basic controlled operations using signals. The project will demonstrate practical understanding of Linux processes, system calls, signals, synchronization, process management, and Operating Systems concepts.

---

## Setup & Execution Instructions

### Prerequisites
- Linux Environment (Ubuntu 20.04+ or WSL2 on Windows)
- GNU Compiler Collection (`gcc`) and `make`:
  ```bash
  sudo apt update && sudo apt install -y build-essential git
  ```

### Building and Running C System Components
1. Clone the repository:
   ```bash
   git clone https://github.com/VishnuVardhan-719/KLH-CSIT-26-27-5-LPMCM.git
   cd KLH-CSIT-26-27-5-LPMCM
   ```
2. Compile C source files (with standard POSIX compliance and warnings enabled):
   ```bash
   gcc -Wall -Wextra -pedantic -std=c11 <source_file>.c -o <output_binary>
   ```
3. Execute the binary:
   ```bash
   ./<output_binary>
   ```
