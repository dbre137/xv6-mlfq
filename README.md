# xv6 MLFQ scheduler (COMPSCI 340 Assignment 4)

This replaces xv6's round-robin scheduler with a multi-level feedback queue
(MLFQ) scheduler. Processes start at the top level and are demoted when they
use up their time allotment, the highest non-empty level always runs, there is
round-robin within a level, and all processes are periodically boosted back to
the top. CPU time is measured with the RISC-V timer and carried over when a
process sleeps, so a process cannot avoid demotion by sleeping just before a
tick. The project also includes a statistics system call, tests, a ps-style
tool and the experiments comparing MLFQ with round-robin.

- Approved plan: [docs/proposal.md](docs/proposal.md)
- Round-robin baseline: [docs/baseline-rr.md](docs/baseline-rr.md)
- Experiment method, results and measurement caveats: [docs/results.md](docs/results.md)

## Requirements

Ubuntu 24.04 LTS:

```sh
sudo apt update
sudo apt install -y build-essential git python3 qemu-system-riscv64 gcc-riscv64-linux-gnu
```

The Makefile also finds Homebrew's `riscv64-unknown-elf` toolchain on macOS.
The test scripts need Python 3 (standard library only) and Bash.

## Build and run

```sh
make clean
make qemu
```

Quit QEMU with `Ctrl-a`, then `x`. xv6 runs on one CPU (`CPUS := 1` in the
Makefile) so that scheduling results are reproducible.

To build without booting:

```sh
make kernel/kernel fs.img
```

## Test

All scripts are run from the repository root. Each one builds the kernel and a
fresh `fs.img`, boots xv6 non-interactively, types commands into its shell and
quits QEMU, so none of them wait for input.

| Command | What it checks | Time |
|---|---|---|
| `scripts/test-mlfq.sh` | `mlfqtest` (all MLFQ tests below), `mlfqps` output and a short `mlfqbench` run | ~1 min |
| `scripts/run-usertests.sh` | every `usertests` quick test, one by one (regression) | ~3 min |
| `scripts/run-xv6.py CMD...` | runs any xv6 shell commands and prints the console output, e.g. `scripts/run-xv6.py 'mlfqtest lecture'` | |

`scripts/test-mlfq.sh` and `scripts/run-usertests.sh` exit with status 0 when
everything passes. `run-usertests.sh` treats the two tests affected by the
README problem (see Known limitations) as expected failures.

`mlfqtest` (in `user/mlfqtest.c`) can also be run inside xv6, either all
tests (`mlfqtest`) or only some (`mlfqtest demote boost`). It prints
`mlfqtest: OK` when every test it ran passed:

| Test | Checks |
|---|---|
| `self`, `unused`, `badptr` | `getpinfo()` lists the caller at level 0, free slots read as zero, and a bad pointer returns -1 |
| `demote` | a CPU-bound process is demoted after its allotment at each level and ends at the lowest level |
| `iobound` | a process that sleeps every tick stays at level 0 |
| `sleepticks` | a sleeping process is not charged CPU time |
| `rr` | two CPU-bound processes at the same level share the CPU |
| `preempt` | a process that becomes runnable at a higher level runs within one tick |
| `boost` | a sleeping and a running process are both back at level 0 after a boost |
| `starve` | a CPU-bound process still gets CPU time with the boost (and starves without it, under the naive accounting rule) |
| `gaming` | a process that runs ~70% of each tick and then sleeps is demoted (and is not, under the naive rule) |
| `killexit` | processes that exit or are killed mid-quantum are cleaned up, and the next process starts clean |
| `lecture` | Lecture 8's scenario: an interactive job stays at level 0 and responsive next to a CPU-bound job |
| `fork` | a forked child starts at level 0, not at its parent's level |

## Demonstration

Inside xv6 (`make qemu`), on one line:

```
mlfqbench mixed 3 & ; mlfqps 2 8
```

This starts 3 CPU-bound and 3 I/O-bound jobs in the background and prints the
process table 8 times, 2 ticks apart. `mlfqps` shows each process's state,
current level and timer ticks at each level: the CPU-bound jobs move from
level 0 to level 1 within a few ticks, while the I/O-bound jobs stay at
level 0. (The jobs are short, so they finish before reaching level 2.)
`mlfqtest starve` and `mlfqtest gaming` show the boost and the accounting rule
working.

## Configuration

All MLFQ settings are in `kernel/param.h`:

| Setting | Default | Meaning |
|---|---|---|
| `SCHED_MLFQ` | 1 | 1 = MLFQ, 0 = xv6's original round-robin scheduler |
| `MLFQ_LEVELS` | 3 | number of levels (0 = highest) |
| `MLFQ_QUANTA` | `{1, 2, 4}` | ticks a process runs at each level before it must yield |
| `MLFQ_ALLOTMENTS` | `{2, 4, 0}` | ticks of CPU at each level before demotion (0 = never) |
| `MLFQ_BOOST_INTERVAL` | 50 | ticks between priority boosts (0 = no boost) |
| `MLFQ_ACCOUNT_ACROSS_YIELDS` | 1 | 1 = CPU time used at a level carries over when a process sleeps; 0 = the naive rule (sleeping restarts the allotment) |

Every setting except `MLFQ_LEVELS` can be overridden for one build without
editing the file. Run `make clean` first, because make does not notice a
changed `MLFQ_DEFS`:

```sh
make clean
make qemu MLFQ_DEFS='-DMLFQ_BOOST_INTERVAL=0 -DMLFQ_ACCOUNT_ACROSS_YIELDS=0'
```

## Experiments

```sh
scripts/run-experiments.sh            # all four experiments, about 40 min
scripts/run-experiments.sh e2         # or just one (e1, e2, e3, e4)
scripts/summarise-results.py          # tables from docs/experiments/raw.txt
```

Each configuration is built with `MLFQ_DEFS` and booted three times, running
`mlfqbench` workloads (`user/mlfqbench.c`). Raw results are appended to
`docs/experiments/raw.txt`. Results and their caveats are in
[docs/results.md](docs/results.md).

## Main changes to xv6

| File | Change |
|---|---|
| `kernel/param.h` | MLFQ settings, `TICK_INTERVAL` |
| `kernel/proc.h` | per-process MLFQ state in `struct proc` (protected by `p->lock`) |
| `kernel/proc.c` | `mlfq_scheduler()`, `mlfq_tick()`, `mlfq_charge()`, `mlfq_boost()`, `kgetpinfo()`; state reset in `allocproc()`/`freeproc()` |
| `kernel/trap.c` | timer interrupts yield only when `mlfq_tick()` says so; `clockintr()` triggers the boost |
| `kernel/pstat.h`, `syscall.*`, `sysproc.c`, `user/usys.pl`, `user/user.h` | the `getpinfo()` system call |
| `user/mlfqtest.c`, `user/mlfqps.c`, `user/mlfqbench.c` | tests, ps-style tool, workloads |
| `scripts/` | test, regression and experiment scripts |

## Assumptions

- One CPU. The scheduler's per-level round-robin cursor is shared between
  CPUs and was not designed or tested for `CPUS > 1` (the multi-CPU stretch
  goal was not attempted).
- A tick is about 0.1 s (`TICK_INTERVAL` timer units), as in the starter code.
- Forked children start at level 0 rather than inheriting their parent's level.

## Known limitations

- **`usertests -q` stops at its first test.** `copyout` and `lazy_copy`
  open a file named `README`, but the Makefile puts `README.xv6` on the disk
  image. This comes from the starter code and was left unchanged;
  `scripts/run-usertests.sh` runs the other quick tests individually, and all
  of them pass.
- **Intermittent stall, cause unknown.** A few times during development and
  in 3 of 24 runs of one experiment, xv6 stopped with a process RUNNABLE
  until a console interrupt arrived. It could not be reproduced in dozens of
  reruns. The test and experiment scripts time out instead of hanging.
- **Measurement caveats** for the experiments (the `waitx()` waiting count,
  tick resolution, CPU time measured against nominal ticks, the calibrated
  gaming workload) are described in [docs/results.md](docs/results.md).
- `getpinfo()`'s `cputime` is measured by the MLFQ scheduler only, so it is
  0 when built with `SCHED_MLFQ=0`.
- Developed and tested on macOS with Homebrew's GCC 16 toolchain. The build
  uses `-Werror`, so a different compiler version may report different
  warnings.
