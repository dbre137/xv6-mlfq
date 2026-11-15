# Baseline: xv6 round-robin scheduler

Proposal step 1. These are the reference numbers for xv6's default
round-robin scheduler (`default_scheduler()` in `kernel/proc.c`), recorded
before any MLFQ changes. The full comparison against MLFQ is step 6.

## Setup

- Commit `5c3675d` (starter scheduler, `CPUS := 1`).
- Host: Apple Silicon Mac, QEMU 11.1.3, Homebrew `riscv64-unknown-elf-gcc` 16.1.0.
- One timer tick is about 0.1 s of QEMU time (`clockintr()` in `kernel/trap.c`).
- Each run boots xv6 from a fresh `fs.img`:

  ```sh
  scripts/run-xv6.py --timeout 300 'uoa_exectime uoa_cpubound 10' 'uoa_exectime uoa_iobound' 'uoa_testschd'
  ```

## What is measured

- `uoa_exectime CMD`: wall-clock ticks (`uptime()`) from fork to `wait()`.
- `uoa_testschd`: starts 5 CPU-bound (`uoa_cpubound 3`) and 5 I/O-bound
  (`uoa_iobound`) processes at once. For each process it reports the
  runnable + sleeping ticks from `waitx()`, then prints the averages. The
  priorities it passes to `execp()` are ignored by round-robin.

## Results (ticks)

| Measurement                                  | Run 1 | Run 2 | Run 3 |
|----------------------------------------------|------:|------:|------:|
| `uoa_exectime uoa_cpubound 10` (alone)       |    24 |    24 |    25 |
| `uoa_exectime uoa_iobound` (alone)           |     4 |     5 |     5 |
| `uoa_testschd` average CPU-bound wait time   |    19 |    19 |    18 |
| `uoa_testschd` average I/O-bound wait time   |    27 |    24 |    25 |

## Observations

- Results are stable to within about 1 tick for the CPU-bound job and
  about 3 ticks for the I/O-bound average, so differences smaller than
  that should not be read as real effects.
- Under round-robin in the mixed workload, the I/O-bound processes wait
  longer on average than the CPU-bound ones. MLFQ should reverse this by
  keeping I/O-bound processes at the top level.
- With the default argument `uoa_cpubound` finishes in about 1 tick, too
  short to measure, so an argument of 10 is used for the single-job timing.
