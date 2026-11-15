# xv6 MLFQ scheduler — COMPSCI 340 Assignment 4

## Project
- Goal: replace xv6's round-robin scheduler with a multi-level feedback queue (MLFQ)
  scheduler, then evaluate it against round-robin.
- `docs/proposal.md` is the approved proposal. It is the plan this project is marked
  against: follow its steps in order, and ask before adding anything outside it.
- Language: C. Marked on Ubuntu 24.04 (`riscv64-linux-gnu-gcc`). Developed on macOS
  with Homebrew's `riscv64-unknown-elf` toolchain, so compiler warnings can differ.

## Build and run
- Build and boot: `make qemu`. Quit QEMU with Ctrl-a, then x.
- Clean rebuild: `make clean && make qemu`.
- `make qemu` is interactive and never exits by itself. For automated runs, use the
  scripted runner in `scripts/` (boots xv6, types commands, captures output, quits).
- The Makefile builds with `-Werror`: any warning fails the build.
- Regression suite: `usertests -q` inside xv6.

## Key files
- `kernel/proc.c`: `scheduler()`, `yield()`, `sched()`, `fork()`, `exit()`, sleep/wakeup.
- `kernel/proc.h`: `struct proc` (per-process MLFQ state goes here).
- `kernel/trap.c`: timer interrupts, which currently call `yield()` on every tick.
- `kernel/param.h`: all MLFQ parameters (levels, quanta, allotments, boost interval).
- New system calls: `kernel/syscall.h`, `kernel/syscall.c`, `kernel/sysproc.c`,
  `user/usys.pl`, `user/user.h`, and `UPROGS` in the `Makefile` for new programs.

## How to work in this repo
- One proposal step at a time. Make small commits with descriptive messages;
  the commit history is assessed.
- Add or update a test with every change. Build and run the relevant tests
  before each commit.
- Keep `CPUS := 1` for all measurements.
- MLFQ parameters live in `kernel/param.h`, never as hard-coded numbers.
- Follow xv6's existing locking: hold `p->lock` when reading or changing another
  process's scheduling state.
- Don't change unrelated starter code.
- After each change, explain in plain language what changed and why. I have to
  explain all of this code myself in a live walkthrough.
- Never commit build artefacts: `*.o`, `*.d`, `*.asm`, `*.sym`, `user/_*`, `fs.img`,
  `kernel/kernel`, `compile_commands.json`, `.idea/`.

## Known quirks
- `usertests` looks for a file named `README`, but the Makefile only puts
  `README.xv6` on the disk image, so the first file test fails until that is fixed.
- xv6's shell has no history or tab completion; arrow keys insert junk characters.
