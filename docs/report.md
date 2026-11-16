# COMPSCI 340 Assignment 4 report: an MLFQ scheduler for xv6

Daniel Breen (dbre137). Code, tests and README: this repository. Approved plan: [proposal.md](proposal.md). Raw results: [results.md](results.md). AI (Claude Code) was used for implementation and drafting, as allowed for this assignment.

## 1. Final design

xv6's round-robin scheduler is replaced by a multi-level feedback queue following the four rules of Lecture 8: run the highest non-empty level, round-robin within a level, demote a process once it has used its level's allotment, and periodically boost every process to the top. There are 3 levels with quanta of 1, 2 and 4 ticks, allotments of 2 and 4 ticks (unlimited at the lowest level) and a boost every 50 ticks. All parameters are in `kernel/param.h` and can be overridden per build (`make MLFQ_DEFS=...`); `SCHED_MLFQ=0` restores the original scheduler for comparison.

- **State and locking.** Each `struct proc` holds its level, CPU time used at that level, a dispatch timestamp, total CPU time, the ticks used of its current quantum and timer ticks seen at each level. All of it is read and written under `p->lock`.
- **Choosing a process.** `mlfq_scheduler()` searches level 0, then 1, then 2 for a RUNNABLE process; a per-level cursor makes the search round-robin, and after every dispatch it starts again at level 0. The queues are implicit (a level field plus a scan of the 64-entry process table) rather than linked lists, so sleep, wakeup, exit and kill need no queue maintenance. The cost is a scan of up to 3 × 64 entries per decision, which is small on one CPU.
- **Timer path.** xv6 previously yielded on every timer tick. Now `mlfq_tick()` charges the running process and yields only if its quantum is used up, it has just been demoted, or a process at a higher level is runnable, which gives preemption within one tick. `clockintr()` calls `mlfq_boost()` every 50 ticks; the boost resets every process, including sleeping ones, to level 0 with a fresh allotment.
- **Time accounting.** CPU time is measured with the RISC-V timer (`r_time()`) from dispatch to the next tick or switch-out, and accumulates across sleeps; a process is demoted when it reaches its allotment (rounded to the nearest tick). The naive rule, where sleeping restarts the allotment, is kept as a build option (`MLFQ_ACCOUNT_ACROSS_YIELDS=0`) for the experiments.
- **Forked children start at level 0** rather than inheriting the parent's level: a child is new work, and xv6's shell forks a child for every command. The cost is that a CPU-bound process could fork to regain priority; a boost would reset it within 50 ticks anyway.
- **Statistics.** A new `getpinfo()` system call copies each slot's level, per-level ticks and measured CPU time to user space; `mlfqps` displays them while the system runs.

## 2. Trade-offs and results

All runs use one CPU, 3 boots per configuration, times in ticks (≈ 0.1 s); full tables in [results.md](results.md).

| Experiment (workload) | Setting | Key result (mean of 3 runs) |
|---|---|---|
| E1 RR vs MLFQ (4 CPU + 4 I/O jobs) | RR → MLFQ | I/O jobs' turnaround 24.0 → 18.3; CPU jobs' 19.4 → 18.6 |
| E2 quanta (mixed) | {1,2,4} / {2,4,8} / {4,8,16} | I/O response 2.5 / 5.0 / 10.0, worst 4 / 8 / 16; CPU turnaround 19.0 / 19.4 / 19.7 |
| E3 boost (starvation), naive rule | 10 / 50 / 200 / off | CPU-bound job's longest gap 8 / 48 / 189 / 300 (starved); CPU ticks 60 / 12 / 2 / 0 |
| E3, accounting across yields | 10 / 50 / 200 / off | longest gap 6 / 16 / 16 / 16; CPU ticks 121 / 169 / 263 / 284 |
| E4 gaming (gamer + CPU job, 200 ticks) | naive → across | gamer's CPU 63.5 → 34.2; CPU-bound job's 145.5 → 174.6 |

- **MLFQ against round-robin (E1).** With CPU- and I/O-bound jobs mixed, keeping the I/O jobs at the top cut their turnaround by about a quarter without slowing the CPU jobs. On one kind of job alone the two schedulers are within noise, as expected.
- **Quantum length (E2).** Longer quanta made response time grow in proportion (the worst case equals the lowest level's quantum) while CPU-bound turnaround did not improve. In QEMU a context switch is cheap compared with a 0.1 s tick, so the throughput side of the trade-off is too small to measure here; short quanta are the better choice on this system.
- **Boost interval (E3).** Under the naive rule the boost interval directly bounds starvation: the longest gap is about one interval, and with no boost the CPU-bound job gets nothing. With accounting across yields the interactive processes are demoted too, so the CPU-bound job is never starved, and frequent boosts actually reduce its share (121 ticks at 10 against 284 with no boost) because each boost lifts the interactive processes back above it. The boost is a safety net whose interval trades prioritisation against the worst-case wait.
- **Accounting (E4).** A process that runs about 70% of each tick and sleeps before the tick ends kept level 0 under the naive rule and took 30% of the CPU; with accounting across yields it was demoted and its share fell to 16%.

## 3. How the implementation changed from the plan

The goals did not change, so no change was emailed; these are the main differences in how they were met.

1. **Measured time instead of tick sampling.** The first version charged a process one tick whenever the timer interrupt found it running, as in step 3. The gaming test showed this cannot work: a process that sleeps just before every tick is never seen by the timer, so "accounting across yields" charged it nothing. Accounting was changed to measure time with `r_time()` (commit `9c49bfc`), and per-level tick counts are kept only as statistics.
2. **Workloads.** The plan named `uoa_cpubound`, `uoa_iobound` and `waitx`. `waitx` is used, but the comparison uses a new program, `mlfqbench`: `uoa_iobound` writes files, so its sleep pattern is set by the disk rather than controlled, and `uoa_testschd` reports wait time as runnable plus sleeping time, which for I/O jobs mostly measures sleeping. Rerunning the step-1 baseline under MLFQ confirmed this: no improvement on `uoa_testschd` (I/O wait 25 → 26), and `uoa_iobound` alone took 6–7 ticks instead of 4–5, which I have not explained.
3. **Starvation test.** A single "hog" left gaps at level 0, so the test uses ping-pong pairs that keep level 0 busy. Because accounting across yields demotes those processes too, starvation without a boost only appears under the naive rule; the test checks both cases.
4. **Tooling and starter fixes.** `make qemu` is interactive, so a scripted runner (`scripts/run-xv6.py`) boots xv6, types commands and checks output for the tests and experiments. Two starter problems were fixed: a GCC 16 build error in `usertests`, and the disk image lacked the `README` file that `usertests` opens, so `usertests -q` now passes.
5. **Stretch goal.** Multiple CPUs were not attempted; the scheduler's cursor is shared and untested with `CPUS > 1`.

## 4. Test results

`mlfqtest` has 14 tests, all passing: the statistics call (including a bad pointer), demotion at each level, an I/O-bound process staying at level 0, no CPU charged while asleep, round-robin within a level, preemption within one tick, boosting both sleeping and running processes, starvation with and without the boost, gaming under both accounting rules, exit and kill mid-quantum, forked children, and the Lecture 8 trace. The tests for each mechanism were also run against a deliberately broken scheduler (for example without preemption, boosting only the running process, or using the naive rule) to confirm they fail when they should.

The Lecture 8 test replays slides 12–13 in ticks: job A from time 0, job B from time 6 running 1 tick and waiting 4. A is at the lowest level when B arrives; B's level after each round is 0, 1, 1, 1, 1, 2, 2, 2, then 0 after the boost; B wakes above A in 9 of 12 rounds and A still gets 48 of 60 ticks. B is not "always at the top": its 1-tick bursts add up to its allotment, which is the accounting rule working. With these parameters A's 4-tick quantum ends on the tick B wakes, so preemption is not visible in this trace and is covered by its own test.

`usertests -q` passes all 58 quick tests. Everything was built and tested on macOS (GCC 16) and on a fresh clone on Ubuntu 24.04 (GCC 13.3).

## 5. Remaining limitations

- **Intermittent stall.** A few times in development, and in 3 of 24 boots of one experiment, a RUNNABLE process did not run until a console interrupt arrived. It could not be reproduced in dozens of reruns, and the cause is unknown; the scripts time out rather than hang.
- **Measurements.** Results have one-tick resolution, ticks run slightly long so measured CPU totals about 4% over elapsed time, and two sets of the same configuration differed by up to 2 ticks; only 3 runs were made per configuration.
- **Design limits.** One CPU only, linear scans rather than real queues, and forking resets a process to the top level.
