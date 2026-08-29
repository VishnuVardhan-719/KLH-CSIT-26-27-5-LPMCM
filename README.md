# Linux Process Monitoring and Control System

## Section / Team Details
- **Section No.:** 12
- **Team No.:** 05
- **Project Title:** Linux Process Monitoring and Control System

## Team Members
| Roll Number | Student Name | Signature |
|---|---|---|
| 2520090166 | R. Vishnu Vardhan | |
| 2520090070 | K. Amruthavalli | |
| 2520090031 | N.Divya Sree | |

## 1. Abstract
The Linux Process Monitoring and Control System is designed to provide a unified environment for observing and managing active Linux processes. Linux offers process information and control through multiple command-line utilities and system interfaces, which can be difficult to study together. This project brings these concepts into a single Linux-based system that displays process IDs, parent-child relationships, process states, and resource usage, while also supporting controlled process operations. The implementation uses Linux/POSIX concepts including the /proc filesystem, process IDs, signals, kill(), fork(), exec(), and wait(). Process information will be collected and presented in a structured form, and signals will be used to observe changes in selected processes. The expected outcome is a working process monitoring and control system that demonstrates practical understanding of process creation, execution, synchronization, signaling, resource monitoring, and Linux system programming.

## 2. Problem Statement
Linux provides process monitoring and control mechanisms through different commands, APIs, and system interfaces. Using these mechanisms separately makes it difficult to obtain a unified view of process states, PIDs, parent-child relationships, resource usage, and process-control operations. The project addresses this problem by developing a Linux-based system that integrates process monitoring and basic process control while demonstrating core Operating Systems and POSIX system-programming concepts.

## 3. Objectives
1. Monitor and display information about active Linux processes.
2. Observe process states, process IDs, parent-child relationships, and resource usage.
3. Provide controlled process-management operations using POSIX/Linux signals.
4. Demonstrate Linux/POSIX process creation, execution, synchronization, and monitoring concepts.

## 4. Proposed Methodology
The system will be implemented in Linux using C/C++ and POSIX/Linux interfaces. It will collect process information from the /proc filesystem and standard system interfaces, organize the information, and display process IDs, states, relationships, and resource usage. Process-control operations will be performed by sending appropriate signals using kill(). fork(), exec(), and wait() will be used to demonstrate process creation, execution, and synchronization. The system will then observe and report resulting process-state changes in a structured manner.

## 5. Operating Systems Concepts / Linux APIs Used
| OS Concept / Linux API / System Call | Purpose in the Project |
|---|---|
| /proc filesystem | Read process information such as status and resource details. |
| PID / PPID / Process states | Identify processes, parent-child relationships, and execution states. |
| POSIX/Linux signals & kill() | Send controlled signals to selected processes. |
| fork(), exec(), wait() | Demonstrate process creation, execution, termination, and synchronization. |

## 6. Individual Contribution
| Roll Number | Student Name | Individual Responsibility |
|---|---|---|
| 2520090166 | R. Vishnu Vardhan | |
| 2520090070 | K. Amruthavalli | |
| 2520090031 | N.Divya Sree | |

## 7. Tools / Platforms / Software Used
| Tool / Platform / Software | Purpose |
|---|---|
| Linux / Ubuntu | Development and execution platform; provides /proc and Linux process interfaces. |
| C / C++ | System-programming language for implementing process monitoring and control. |
| GCC | Compile and build the C/C++ source code. |
| Git / GitHub | Version control, collaboration, and project repository management. |

## 8. Expected Outcome
A working Linux-based Process Monitoring and Control System that displays active-process information, process states, resource usage, and parent-child relationships, and performs basic controlled operations using signals. The project will demonstrate practical understanding of Linux processes, system calls, signals, synchronization, process management, and Operating Systems concepts.
