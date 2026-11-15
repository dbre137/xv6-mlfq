# Experiment results (proposal step 6)

Measurements comparing xv6's round-robin scheduler with the MLFQ
scheduler and its parameters. This file records **what was measured and
how**; analysis and conclusions belong in the report.

## Setup

- One CPU (`CPUS := 1`), QEMU 11.1.3 on an Apple Silicon Mac, Homebrew
  `riscv64-unknown-elf-gcc` 16.1.0. One tick ≈ 0.1 s.
- E1, E2, E4: commit `ec4d6b6`. E3: commit `b79e663`. The kernel and
  `mlfqbench` are the same in both; only the experiment scripts differ.
- Each configuration is built with `make MLFQ_DEFS=...` (see
  `kernel/param.h`) and booted 3 times from a fresh build. Every run prints
  a `CONFIG` line with the parameters it was compiled with, so each result
  can be traced to its settings.
- Default MLFQ: 3 levels, quanta {1, 2, 4} ticks, allotments {2, 4, ∞}
  ticks, boost every 50 ticks, accounting across yields.

To reproduce (about 40 minutes), then regenerate the tables below:

```sh
scripts/run-experiments.sh            # or: scripts/run-experiments.sh e2
scripts/summarise-results.py
```

Raw output: [experiments/raw.txt](experiments/raw.txt). Tables show the
mean over runs, with the range in brackets when runs differ. Times are in
ticks.

## Workloads (`user/mlfqbench.c`)

| Experiment | Command | What it does |
|---|---|---|
| E1 | `mlfqbench cpu 4` | 4 CPU-bound jobs (30M loop iterations each), forked together |
| E1 | `mlfqbench io 4` | 4 I/O-bound jobs: 10 rounds of a little work and a 1-tick sleep |
| E1, E2 | `mlfqbench mixed 4` | 4 CPU-bound and 4 I/O-bound jobs, interleaved |
| E3 | `mlfqbench starve` | 1 CPU-bound process and 2 ping-pong pairs (always one runnable, sleeping after every message) for 300 ticks |
| E4 | `mlfqbench game` | 1 CPU-bound process and 1 gamer (wakes each tick, runs ~70% of it, sleeps before the next) for 200 ticks |

Measurements:
- **turnaround**: fork to exit (`uptime()` in the parent).
- **waiting**: ticks spent RUNNABLE, from `waitx()`.
- **response**: fork to the child's first run (`uptime()` in each).
- **cputicks / maxgap** (E3): CPU ticks the CPU-bound process got in the
  window, sampled each tick with `getpinfo()`, and the longest run of
  consecutive ticks in which it got none.
- **cpubound / gamer** (E4): CPU time measured by the kernel (`getpinfo()`
  `cputime`, in nominal ticks).

## Results

### E1: round-robin vs MLFQ

| Config | Workload | Runs | n | turnaround | waiting | response | maxresponse |
|---|---|---:|---:|---:|---:|---:|---:|
| e1-rr | cpu: cpu | 3 | 4.0 | 19.4 (18.7-19.7) | 13.8 (13.5-14) | 1.5 | 3.0 |
| e1-rr | io: io | 3 | 4.0 | 10.2 (10-10.5) | 0.2 (0-0.7) | 0.1 (0-0.2) | 0.3 (0-1) |
| e1-rr | mixed: cpu | 3 | 4.0 | 19.4 (18.7-20) | 13.5 (13.2-13.7) | 2.1 (1.5-3.2) | 4.3 (3-7) |
| e1-rr | mixed: io | 3 | 4.0 | 24.0 (23.5-24.5) | 20.0 | 2.7 (2.5-3.2) | 4.0 |
| e1-mlfq | cpu: cpu | 3 | 4.0 | 18.3 (18-18.5) | 13.4 (13.2-13.5) | 1.5 | 3.0 |
| e1-mlfq | io: io | 3 | 4.0 | 10.0 | 0.0 | 0.0 | 0.0 |
| e1-mlfq | mixed: cpu | 3 | 4.0 | 18.6 (18.2-19.5) | 13.2 (13-13.5) | 1.5 | 3.0 |
| e1-mlfq | mixed: io | 3 | 4.0 | 18.3 (18-19) | 17.7 (17.5-18) | 2.5 | 4.0 |

- `e1-rr`: sched=rr quanta=1,2,4 allotments=2,4,0 boost=50 across=1
- `e1-mlfq`: sched=mlfq quanta=1,2,4 allotments=2,4,0 boost=50 across=1

### E2: quantum length (mixed workload)

| Config | Workload | Runs | n | turnaround | waiting | response | maxresponse |
|---|---|---:|---:|---:|---:|---:|---:|
| e2-q1,2,4 | mixed: cpu | 3 | 4.0 | 19.0 (18.5-19.2) | 14.0 (13.5-14.2) | 1.5 | 3.0 |
| e2-q1,2,4 | mixed: io | 3 | 4.0 | 16.2 | 16.2 | 2.5 | 4.0 |
| e2-q2,4,8 | mixed: cpu | 3 | 4.0 | 19.4 (18.2-20) | 14.5 (13.5-15) | 3.0 | 6.0 |
| e2-q2,4,8 | mixed: io | 3 | 4.0 | 24.2 | 19.7 (19-20) | 5.0 | 8.0 |
| e2-q4,8,16 | mixed: cpu | 3 | 4.0 | 19.7 (19-20) | 13.5 | 6.0 | 12.0 |
| e2-q4,8,16 | mixed: io | 3 | 4.0 | 28.0 (27.5-28.2) | 19.7 (19-20) | 10.0 | 16.0 |

- `e2-q1,2,4`: sched=mlfq quanta=1,2,4 allotments=2,4,0 boost=50 across=1
- `e2-q2,4,8`: sched=mlfq quanta=2,4,8 allotments=4,8,0 boost=50 across=1
- `e2-q4,8,16`: sched=mlfq quanta=4,8,16 allotments=8,16,0 boost=50 across=1

### E3: boost interval (starvation workload)

| Config | Workload | Runs | window | cputicks | maxgap |
|---|---|---:|---:|---:|---:|
| e3-across0-boost10 | starve | 3 | 300.0 | 60.0 | 8.0 |
| e3-across0-boost50 | starve | 3 | 300.0 | 12.0 | 48.0 |
| e3-across0-boost200 | starve | 3 | 300.0 | 2.0 | 188.7 (188-189) |
| e3-across0-boost0 | starve | 3 | 300.0 | 0.0 | 300.0 |
| e3-across1-boost10 | starve | 3 | 300.0 | 121.0 | 6.0 |
| e3-across1-boost50 | starve | 3 | 300.0 | 169.3 (168-171) | 16.0 |
| e3-across1-boost200 | starve | 3 | 300.0 | 263.0 | 16.0 |
| e3-across1-boost0 | starve | 3 | 300.0 | 284.0 | 16.0 |

- `e3-across0-boost10`: sched=mlfq quanta=1,2,4 allotments=2,4,0 boost=10 across=0
- `e3-across0-boost50`: sched=mlfq quanta=1,2,4 allotments=2,4,0 boost=50 across=0
- `e3-across0-boost200`: sched=mlfq quanta=1,2,4 allotments=2,4,0 boost=200 across=0
- `e3-across0-boost0`: sched=mlfq quanta=1,2,4 allotments=2,4,0 boost=0 across=0
- `e3-across1-boost10`: sched=mlfq quanta=1,2,4 allotments=2,4,0 boost=10 across=1
- `e3-across1-boost50`: sched=mlfq quanta=1,2,4 allotments=2,4,0 boost=50 across=1
- `e3-across1-boost200`: sched=mlfq quanta=1,2,4 allotments=2,4,0 boost=200 across=1
- `e3-across1-boost0`: sched=mlfq quanta=1,2,4 allotments=2,4,0 boost=0 across=1

### E4: time accounting (gaming workload)

| Config | Workload | Runs | window | elapsed | cpubound | gamer |
|---|---|---:|---:|---:|---:|---:|
| e4-across0 | game | 3 | 200.0 | 200.0 | 145.5 (139.7-152.7) | 63.5 (55.6-69.3) |
| e4-across1 | game | 3 | 200.0 | 200.0 | 174.6 (173.6-175.9) | 34.2 (33.5-34.8) |

- `e4-across0`: sched=mlfq quanta=1,2,4 allotments=2,4,0 boost=50 across=0
- `e4-across1`: sched=mlfq quanta=1,2,4 allotments=2,4,0 boost=50 across=1

## Measurement caveats

These affect how the numbers can be read; they apply equally to every
configuration unless stated.

1. **"waiting" counts the tick a process is woken.** `waitx()`'s count
   comes from the starter's `update_process_times()`, which runs on each
   timer tick only while some process is running (not when the CPU is
   idle), and after that tick has woken sleepers. So an I/O job woken by a
   tick while another process holds the CPU counts as waiting for that
   tick even if it runs at once. This is why `io 4` alone shows ~0 waiting
   but the I/O jobs in `mixed 4` show waiting close to their turnaround.
2. **Tick resolution.** Turnaround and response are measured with
   `uptime()`, so differences under about one tick are within noise.
3. **CPU time vs ticks.** `clockintr()` schedules the next tick 1,000,000
   timer units after the interrupt is *handled*, so real ticks are slightly
   longer than nominal. Kernel-measured CPU time (E4) therefore totals a
   few percent more than the elapsed ticks (e.g. 209 of 200); compare
   shares rather than absolute values.
4. **The E4 gamer is calibrated in loop iterations.** It runs a loop sized
   to ~70% of one tick, measured once at start-up. QEMU's speed on this host
   was not constant (an earlier version of E4 that measured CPU by work done
   gave impossible totals), so the gamer does not always use exactly 70% of
   a tick. Its CPU share is measured correctly by the kernel, but its
   behaviour is only approximately "70% then sleep". The `gaming` test in
   `user/mlfqtest.c`, which checks the gamer's level rather than its share,
   is the more direct evidence of the accounting rule's effect.
5. **E3 with accounting across yields.** The ping-pong processes are
   charged for the CPU they use and are demoted too, so this workload does
   not keep level 0 busy under that rule. Starvation without a boost only
   occurs here under the naive rule.
6. **`cputime` is MLFQ-only.** It is measured by the MLFQ scheduler's
   accounting, so it is 0 under the round-robin baseline; E4 compares MLFQ
   settings only.

## Open issue: intermittent stalls

Occasionally xv6 stalled: a process was RUNNABLE but did not run until an
interrupt from the console arrived. This was seen three times during
step 3 development, and in 3 of the 24 E3 boots in the first full set of
experiments (all E3 with accounting across yields, boost 10 or 50). Those
three runs are in `raw.txt` as configurations with fewer than 3 results;
E3 was then rerun in full with no failures. Reruns of the same settings
(6 × `mlfqbench starve`, 31 × `mlfqtest`, 6 full `usertests` runs under
both schedulers) did not reproduce it, and the cause is not known.
`scripts/run-experiments.sh` now records any failed run as an `ERROR`
line instead of leaving it out.
