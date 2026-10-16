# Results

Evidence captured from real runs on Ubuntu 24.04 (WSL2), GCC 13.3 —
see `RUNSTAMP.txt` for the capture date. The program binary is
`build/lpmcs`; every value shown in the captures is read live from
/proc at capture time.

| File | What it shows |
|---|---|
| `evidence-build.txt` | `make clean && make` — zero warnings with `-Wall -Wextra -std=c11` |
| `evidence-once.txt` | `./build/lpmcs --once` — one-shot process table with real host/CPU/memory header |
| `evidence-tree.txt` | `./build/lpmcs --tree` — process hierarchy built from PID/PPID |
| `evidence-demo.txt` | `./build/lpmcs --demo` — fork() -> execl() -> waitpid() transcript |
| `evidence-test.txt` | historical pre-enhancement test capture; run `make test` for the current 104 + 2 checks |
| `evidence-tui-table.txt` | interactive monitor in a pseudo-tty — live table frames (ANSI stripped) |
| `evidence-tui-safety.txt` | safety demonstration — Pause requested on PID 1, monitor refuses: "PID 1 is protected (init/system)" |

Notes:

- `make sanitize` (AddressSanitizer + UBSan, leak detection) was run
  during development with zero findings; see `test-plan.md` for the
  full plan and the manual scenario checklist.
- The TUI captures were taken inside a pseudo-tty (`script -qec`),
  then ANSI escape sequences were removed for readability; the
  refusal message in `evidence-tui-safety.txt` is the program's real
  output, verbatim.
