# Assignment 4 proposal

## Proposal email

Hi Talia, Thomas and Zalan,

Please find my Assignment 4 proposal below. AI was used to help put this together.

### 1. What I would like to build

I would like to replace xv6's round-robin scheduler with a multi-level feedback queue (MLFQ) scheduler, following the rules covered in Lecture 8 (Advanced Scheduling) and OSTEP Chapter 8. Instead of priorities being set by hand, the scheduler will learn them from each process's behaviour:

- new processes start at the highest level;
- the highest non-empty level always runs, with round-robin within a level;
- a process that uses up its time allotment at a level is demoted;
- all processes are periodically boosted back to the top level to prevent starvation.

Time will be accounted across yields, so a process cannot avoid demotion by giving up the CPU just before its quantum ends. I will then evaluate the scheduler against xv6's default round-robin scheduler using CPU-bound, I/O-bound and mixed workloads, and compare different parameter choices.

### 2. Language and parts of the OS I expect to change

I will use C. I expect to change:

- kernel/proc.h: per-process MLFQ state (current level, time used at that level, ticks run at each level).
- kernel/proc.c: the scheduler loop, process creation, fork and exit.
- kernel/trap.c: timer preemption. xv6 currently yields on every timer tick; with MLFQ a running process should only be preempted when its quantum expires or a higher-level process becomes runnable. The periodic boost will also be driven from the timer.
- kernel/param.h: the number of levels, the quantum and allotment per level, and the boost interval, as configurable parameters (initially 3 levels with quanta of 1, 2 and 4 ticks and a boost every 50 ticks, to be tuned).
- A new system call (with the usual syscall.c, syscall.h, sysproc.c, usys.pl and user.h changes) reporting each process's level and per-level CPU ticks.
- New user programs for tests and workloads, and a ps-style tool that shows levels while the system runs.

### 3. How this differs from Assignments 1-3

The brief lists taking a mechanism studied earlier, implementing a more advanced version and comparing the design choices as a possible direction, and this project follows that direction. Assignment 1's scheduler used two static priorities that processes set themselves with setpri(), on a single CPU, with the timer still preempting on every tick. This project goes substantially further:

- priorities are adaptive and inferred from behaviour rather than user-set;
- there are several levels with different quanta;
- starvation must be prevented with a priority boost;
- the scheduler must resist allotment gaming;
- the timer-interrupt preemption path changes, which Assignment 1 did not touch.

It also includes a quantitative evaluation, which none of the earlier assignments required.

### 4. Design choices and trade-offs I expect to explore

- Quantum length per level: short quanta at the top give interactive processes good response time but cause more context switches. I will measure response time against throughput for several settings.
- Boost interval: frequent boosts make MLFQ behave like round-robin and weaken prioritisation, while infrequent boosts let CPU-bound processes starve behind interactive ones. I will measure the longest gap a CPU-bound process waits for the CPU under different intervals.
- Time accounting: a naive rule that resets a process's time whenever it yields lets a process game the scheduler by sleeping just before its quantum expires. I will compare the naive rule with accounting across yields, using a deliberately gaming test program.

I will also decide and document whether forked children start at the top level or inherit their parent's level.

### 5. Main steps

Core goals:

1. Study the existing scheduler and timer path, set CPUS to 1 for reproducible measurements, and record baseline results with round-robin.
2. Add the per-process MLFQ state and the statistics system call.
3. Implement level selection, round-robin within a level and demotion, including the change to timer preemption.
4. Add the periodic priority boost.
5. Add accounting across yields to prevent gaming.
6. Run the experiments and write the report.

Stretch goal, only if time allows: extend the scheduler to multiple CPUs and examine how per-level queues interact with several CPUs scheduling at once.

I will use Git throughout with small, descriptive commits, adding tests alongside each step rather than at the end.

### 6. Testing and demonstration

- Behaviour tests using the statistics system call: a CPU-bound process reaches the lowest level within the expected number of ticks, an I/O-bound process stays at the top level, and a boost returns every process to the top.
- Lecture trace: reproduce Lecture 8's scenario of one CPU-bound job and one interactive job, and check that the interactive job stays responsive while the CPU-bound job still makes progress.
- Starvation test: one CPU-bound process alongside many I/O-bound processes, showing it receives CPU time within each boost interval, and that it starves when boosting is disabled.
- Gaming test: a program that sleeps just before its quantum expires, showing it is demoted with accounting across yields but not under the naive rule.
- Edge cases: processes that exit or are killed mid-quantum, forked children, and sleeping processes during a boost.
- Comparison: turnaround, waiting and response times for CPU-bound, I/O-bound and mixed workloads (using uoa_cpubound, uoa_iobound and waitx) under round-robin and several MLFQ configurations, presented as tables in the report.
- Regression: usertests -q should still pass.

For the demonstration, I will run the mixed workload with the ps-style tool showing processes moving between levels, then run the starvation and gaming tests.

Thank you for considering my proposal.

Kind regards,
Daniel Breen

## Reply from the teaching staff

Hi Daniel,

Sorry for the late reply -- this looks good to us. Please make sure you submit it by the last day of the examination period.
