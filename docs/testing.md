# Testing Strategy

## Running the tests

```bash
make test        # 104 C checks plus 2 end-to-end CLI checks
make sanitize    # same suite under AddressSanitizer + UBSan
```

Both targets build `build/run_tests` from the same sources as the
application. The C runner ends with `104 checks, 0 failures` /
`ALL TESTS PASSED`; shell checks also verify clean forked output and
non-zero interval CPU in `--once`. Any failure returns a non-zero status.

## Test organization

Everything lives in one file, `tests/run_tests.c`, with a tiny
check-counting framework (`ok()` / `group()`) and sixteen groups:

| Group | Covers |
|---|---|
| utils | safe string copy/truncate, trim, case-insensitive search, uptime format, UTF-8 width |
| system_info | hostname/kernel/CPU/MemTotal from the real machine, meminfo parsing incl. prefix-key confusion (MemTotal vs HugePages_Total) |
| parse_stat_line | synthetic /proc stat lines, including `(comm) with spaces` and starttime identity, plus malformed input |
| parse_status_field | Uid (first tab field), VmRSS ("5124 kB" kept intact), missing key, NULL buffer |
| cpu_percent_of | 20/100 = 20%, 50/1000 = 5%, zero system delta, backwards counters, all zeros |
| state_char_name | R S D T Z I mapping, UNKNOWN fallback |
| search/filter | name/user/PID/case-insensitive matching, state filter, "match all" cases |
| monitor discovery | real /proc scan: PID 1 and the test process itself found, plausible count, second refresh on a reused buffer |
| top-style process CPU | a real one-thread CPU burner reports near 100%, not total-machine share |
| CPU history pruning | colliding PIDs retain history when an earlier hash entry is pruned |
| get_process_info | PID 1 readable with state name; negative and nonexistent PIDs rejected |
| disappearing process | fork + exec /bin/true, reap, then a graceful ENOENT on the exited PID |
| control safety gates | PID 1/0/negative, the monitor itself and its parent refused with explanatory text |
| control round-trips | PID identity check; SIGSTOP/SIGCONT/SIGTERM/SIGKILL lifecycle on disposable children |
| process tree | a forked child appears in the live tree; deterministic root |
| process tree cycle | malformed cyclic parentage renders each PID once without recursive duplication |

Destructive signal tests run **only** against disposable children the
suite forks itself; the children either `pause()` forever or exec
`/bin/true`. No system process is ever signalled.

## Live tests (real /proc on the running machine)

- `monitor_create` + `monitor_refresh` discover a realistic process
  count; PID 1 and the monitor's own process are present with valid
  state, user and parent.
- Reading a negative PID and a nonexistent PID (kernel rejects,
  ENOENT path) fails cleanly instead of crashing.
- The CPU hash is exercised with a deliberate collision, removal and
  surviving busy process; PID plus starttime defines process identity.

## Sanitizer policy

`make sanitize` recompiles the suite with
`-fsanitize=address,undefined -fno-omit-frame-pointer` and runs it
with `ASAN_OPTIONS=detect_leaks=1`. The suite must pass with **zero**
ASan/UBSan reports — this is the memory-safety gate (buffer
overflows, use-after-free, leaks, signed overflow, misaligned
access). The interactive binary is built from the same sources, so a
clean sanitizer run over the parsing/tree/monitor/control paths gives
high confidence in the UI runtime as well.

## Manual verification checklist

For the interactive screens (not automatable):

1. `make run` — table auto-refreshes each second; arrow keys move the
   selection; the status line shows sort/filter/search state.
2. `Enter` on a row — details show exe, cmdline, threads, nice.
3. `s`/`f`/`/` — sort popup, filter popup and search editing behave;
   Esc cancels.
4. `t` — tree view renders the real hierarchy; scrolling works.
5. `d` — demo screen prints the fork/exec/waitpid sequence once.
6. Start `sleep 300 &` in another shell, Pause it (state T visible),
   Resume it, then Stop it — the row disappears with the
   "no longer running" status.
7. Try to control a root-owned process as a normal user — EPERM is
   reported as a status message; no crash.
8. Attempt SIGKILL — the y/N confirmation appears; default (Enter) is No.
9. `q` — terminal is restored exactly (cursor visible, normal buffer).
