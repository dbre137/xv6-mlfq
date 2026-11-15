// Tests for the MLFQ scheduler's per-process state, read through the
// getpinfo() system call. Prints "mlfqtest: OK" if every test passes.

#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/pstat.h"
#include "user/user.h"

// enum procstate values from kernel/proc.h
#define ST_UNUSED   0
#define ST_SLEEPING 2
#define ST_RUNNING  4

// Too big for the one-page user stack, so keep it global.
static struct pstat ps;

static void
fail(char *test, char *why)
{
  printf("mlfqtest: %s FAILED: %s\n", test, why);
  exit(1);
}

// Refresh ps and return pid's entry, or 0 if it is not in the table.
static struct pstat_entry *
lookup(char *test, int pid)
{
  if (getpinfo(&ps) < 0)
    fail(test, "getpinfo returned -1");
  for (int i = 0; i < NPROC; i++)
    if (ps.proc[i].state != ST_UNUSED && ps.proc[i].pid == pid)
      return &ps.proc[i];
  return 0;
}

// The calling process is listed, running, at the top level.
static void
test_self(void)
{
  char *t = "self";
  struct pstat_entry *e = lookup(t, getpid());
  if (e == 0)
    fail(t, "own pid not in table");
  if (e->state != ST_RUNNING)
    fail(t, "own state is not RUNNING");
  if (strcmp(e->name, "mlfqtest") != 0)
    fail(t, "wrong name");
  if (e->level != 0)
    fail(t, "not at level 0");
  printf("mlfqtest: %s OK\n", t);
}

// Free slots read as all zeros.
static void
test_unused(void)
{
  char *t = "unused";
  if (getpinfo(&ps) < 0)
    fail(t, "getpinfo returned -1");
  for (int i = 0; i < NPROC; i++) {
    struct pstat_entry *e = &ps.proc[i];
    if (e->state != ST_UNUSED)
      continue;
    if (e->pid != 0 || e->level != 0 || e->ticks_used != 0)
      fail(t, "unused slot has non-zero fields");
    for (int l = 0; l < MLFQ_LEVELS; l++)
      if (e->ticks[l] != 0)
        fail(t, "unused slot has non-zero ticks");
  }
  printf("mlfqtest: %s OK\n", t);
}

// Spin in user mode for about n timer ticks of wall-clock time.
// Check the clock only occasionally: uptime() runs with interrupts
// off while it holds tickslock, so calling it in a tight loop would
// make nearly every timer interrupt arrive in the kernel instead.
static void
spin(int n)
{
  int start = uptime();
  while (uptime() - start < n)
    for (volatile int i = 0; i < 1000000; i++)
      ;
}

static uint
total_ticks(struct pstat_entry *e)
{
  uint sum = 0;
  for (int l = 0; l < MLFQ_LEVELS; l++)
    sum += e->ticks[l];
  return sum;
}

// Make sure no priority boost happens in the next n ticks, by
// sleeping past the next boost if it is due sooner. Tests that check
// exact levels use this so a boost cannot land in the middle.
static void
avoid_boost(int n)
{
#if MLFQ_BOOST_INTERVAL > 0
  int now = uptime();
  int next_boost = (now / MLFQ_BOOST_INTERVAL + 1) * MLFQ_BOOST_INTERVAL;
  if (next_boost - now <= n)
    pause(next_boost - now + 1);
#endif
}

// Run body() in a fresh child, which starts at level 0, and exit if
// it fails. Earlier tests leave this process at the lowest level,
// where it would compete for the CPU with the processes it watches.
static void
run_in_child(char *test, void (*body)(void))
{
  int pid = fork();
  if (pid < 0)
    fail(test, "fork failed");
  if (pid == 0) {
    body();
    exit(0);
  }
  int status;
  wait(&status);
  if (status != 0)
    exit(1);
}

// Use n ticks of this process's own CPU time (not wall-clock time,
// which also counts time other processes are running).
static void
burn(int n)
{
  struct pstat_entry *e = lookup("burn", getpid());
  uint target = total_ticks(e) + n;
  while (total_ticks(lookup("burn", getpid())) < target)
    for (volatile int i = 0; i < 1000000; i++)
      ;
}

// Pause until pid's entry satisfies level == want_level and (if
// want_state >= 0) state == want_state; fail after 100 ticks.
static struct pstat_entry *
await_level(char *test, int pid, int want_level, int want_state)
{
  for (int k = 0; k < 100; k++) {
    struct pstat_entry *e = lookup(test, pid);
    if (e != 0 && e->level == want_level &&
        (want_state < 0 || e->state == want_state))
      return e;
    pause(1);
  }
  struct pstat_entry *e = lookup(test, pid);
  printf("mlfqtest: %s: pid %d is at level %d in state %d, wanted level %d\n",
         test, pid, e ? e->level : -1, e ? e->state : -1, want_level);
  fail(test, "timed out waiting for a process to reach a level");
  return 0;
}

// A CPU-bound process is demoted once it has used its level's
// allotment (allotments {2, 4, 0} in kernel/param.h), then stays at
// the lowest level. CPU time is measured exactly but rounded to the
// nearest tick, so the child records 2 timer ticks at level 0, or 3
// if it was first dispatched late in a tick, and then 4 at level 1.
static void
test_demote(void)
{
  char *t = "demote";
  avoid_boost(15);
  int pid = fork();
  if (pid < 0)
    fail(t, "fork failed");
  if (pid == 0) {
    spin(10);
    struct pstat_entry *e = lookup(t, getpid());
    uint total = total_ticks(e);
    printf("mlfqtest: %s: level %d, ticks %d/%d/%d\n", t, e->level,
           e->ticks[0], e->ticks[1], e->ticks[2]);
    if (e->ticks[0] < 2 || e->ticks[0] > 3 || e->ticks[1] != 4)
      fail(t, "expected 2-3 ticks at level 0 and 4 at level 1");
    if (e->level != MLFQ_LEVELS - 1)
      fail(t, "not at the lowest level");
    // On one CPU with nothing else runnable, it gets nearly every tick.
    if (total < 8 || total > 11)
      fail(t, "expected 8-11 ticks in a 10-tick spin");
    exit(0);
  }
  int status;
  wait(&status);
  if (status != 0)
    exit(1);
  printf("mlfqtest: %s OK\n", t);
}

// An I/O-bound process, which sleeps every tick and uses very little
// CPU, stays at the top level.
static void
test_iobound(void)
{
  char *t = "iobound";
  int pid = fork();
  if (pid < 0)
    fail(t, "fork failed");
  if (pid == 0) {
    for (int k = 0; k < 20; k++)
      pause(1);
    struct pstat_entry *e = lookup(t, getpid());
    if (e->level != 0)
      fail(t, "I/O-bound process left level 0");
    for (int l = 1; l < MLFQ_LEVELS; l++)
      if (e->ticks[l] != 0)
        fail(t, "I/O-bound process ran below level 0");
    exit(0);
  }
  int status;
  wait(&status);
  if (status != 0)
    exit(1);
  printf("mlfqtest: %s OK\n", t);
}

// A process that sleeps is not charged for the time it sleeps.
static void
test_sleepticks(void)
{
  char *t = "sleepticks";
  int pid = fork();
  if (pid < 0)
    fail(t, "fork failed");
  if (pid == 0) {
    pause(10);
    struct pstat_entry *e = lookup(t, getpid());
    exit(e != 0 && total_ticks(e) <= 1 ? 0 : 1);
  }
  int status;
  wait(&status);
  if (status != 0)
    fail(t, "sleeping child was charged CPU ticks");
  printf("mlfqtest: %s OK\n", t);
}

// Two CPU-bound processes at the same level share the CPU
// (round-robin within a level): over a 6-tick window each gets at
// least 2 ticks, rather than one running while the other starves.
static void
test_rr(void)
{
  char *t = "rr";
  int pids[2];

  for (int k = 0; k < 2; k++) {
    pids[k] = fork();
    if (pids[k] < 0)
      fail(t, "fork failed");
    if (pids[k] == 0) {
      spin(12);
      exit(0);
    }
  }
  pause(6);
  for (int k = 0; k < 2; k++) {
    struct pstat_entry *e = lookup(t, pids[k]);
    if (e == 0)
      fail(t, "child exited too early");
    if (total_ticks(e) < 2)
      fail(t, "a child got fewer than 2 of 6 ticks");
  }
  for (int k = 0; k < 2; k++) {
    kill(pids[k]);
    wait(0);
  }
  printf("mlfqtest: %s OK\n", t);
}

// A process that becomes runnable at a higher level preempts a
// lower-level process mid-quantum, within one tick, instead of
// waiting for the lower-level process's quantum (4 ticks at level 2).
static void
test_preempt(void)
{
  char *t = "preempt";

  avoid_boost(20);
  spin(7);  // use up the level-0 and level-1 allotments (2 + 4 ticks)
  struct pstat_entry *me = lookup(t, getpid());
  if (me == 0 || me->level != MLFQ_LEVELS - 1)
    fail(t, "parent should be at the lowest level");

  pause(1);  // come back with a fresh 4-tick quantum
  int t0 = uptime();
  int pid = fork();
  if (pid < 0)
    fail(t, "fork failed");
  if (pid == 0) {
    int waited = uptime() - t0;
    if (waited > 1) {
      printf("mlfqtest: %s: child waited %d ticks\n", t, waited);
      exit(1);
    }
    exit(0);
  }
  spin(6);  // stay CPU-bound while the child is waiting
  int status;
  wait(&status);
  if (status != 0)
    fail(t, "level-0 child waited more than 1 tick to run");
  printf("mlfqtest: %s OK\n", t);
}

// The periodic boost (every MLFQ_BOOST_INTERVAL ticks) moves every
// process back to level 0: one that is asleep during the boost, and
// one that is CPU-bound and runnable.
static void
boost_body(void)
{
  char *t = "boost";
#if MLFQ_BOOST_INTERVAL == 0
  printf("mlfqtest: %s skipped (MLFQ_BOOST_INTERVAL is 0)\n", t);
#else
  int low = MLFQ_LEVELS - 1;
  int fds[2];

  // The sleeper must reach the lowest level (7 ticks of CPU while
  // sharing it with the runner) before the next boost.
  avoid_boost(35);
  int deadline = uptime() + 150;  // helpers exit by then regardless
  if (pipe(fds) < 0)
    fail(t, "pipe failed");

  int runner = fork();
  if (runner < 0)
    fail(t, "fork failed");
  if (runner == 0) {
    while (uptime() < deadline)
      burn(1);
    exit(0);
  }
  int sleeper = fork();
  if (sleeper < 0)
    fail(t, "fork failed");
  if (sleeper == 0) {
    burn(7);  // past the level-0 and level-1 allotments (2 + 4)
    int level = lookup(t, getpid())->level;
    write(fds[1], &level, sizeof(level));  // report, then sleep
    pause(deadline - uptime());
    exit(0);
  }

  int level;
  if (read(fds[0], &level, sizeof(level)) != sizeof(level))
    fail(t, "no report from sleeper");
  if (level != low)
    fail(t, "sleeper did not reach the lowest level");
  struct pstat_entry *r = lookup(t, runner);
  if (r == 0 || r->level == 0)
    fail(t, "runner should have left level 0 by now");
  uint before = r->ticks[0];

  // Sleep until just after the next boost.
  int now = uptime();
  int next_boost = (now / MLFQ_BOOST_INTERVAL + 1) * MLFQ_BOOST_INTERVAL;
  pause(next_boost - now + 1);

  struct pstat_entry *e = lookup(t, sleeper);
  if (e == 0 || e->state != ST_SLEEPING || e->level != 0)
    fail(t, "sleeping process was not boosted to level 0");
  r = lookup(t, runner);
  if (r == 0 || r->ticks[0] <= before)
    fail(t, "CPU-bound process did not run at level 0 after the boost");

  kill(runner);
  kill(sleeper);
  wait(0);
  wait(0);
  printf("mlfqtest: %s OK\n", t);
#endif
}

static void
test_boost(void)
{
  run_in_child("boost", boost_body);
}

// Starvation: a CPU-bound process at the lowest level runs alongside
// two pairs of interactive processes that pass a byte back and forth
// through pipes. In each pair one process is always runnable while
// the other blocks in read(), so level 0 is never empty; and as each
// one sleeps after every message, it never uses up its allotment and
// stays at level 0. With the boost, the CPU-bound process still gets
// CPU time in every boost interval. With MLFQ_BOOST_INTERVAL 0 and
// the naive accounting rule it starves.
#define NPAIRS 2
static void
pingpong(int in, int out, int deadline)
{
  char c = 0;
  while (uptime() < deadline) {
    if (out >= 0)
      write(out, &c, 1);
    if (read(in, &c, 1) != 1)
      exit(0);
    out = out < 0 ? -out - 1 : out;
  }
  exit(0);
}

static void
starve_body(void)
{
  char *t = "starve";
  int low = MLFQ_LEVELS - 1;
  int kids[2 * NPAIRS];

  int deadline = uptime() + 200;  // helpers exit by then regardless
  int cpu = fork();
  if (cpu < 0)
    fail(t, "fork failed");
  if (cpu == 0) {
    while (uptime() < deadline)
      burn(1);
    exit(0);
  }
  await_level(t, cpu, low, -1);

  for (int k = 0; k < NPAIRS; k++) {
    int ab[2], ba[2];
    if (pipe(ab) < 0 || pipe(ba) < 0)
      fail(t, "pipe failed");
    if ((kids[2 * k] = fork()) == 0)
      pingpong(ba[0], ab[1], deadline);      // A: writes first
    if ((kids[2 * k + 1] = fork()) == 0)
      pingpong(ab[0], -ba[1] - 1, deadline); // B: waits for A first
    close(ab[0]); close(ab[1]); close(ba[0]); close(ba[1]);
  }

  pause(5);  // let the pairs get going
  struct pstat_entry *e = lookup(t, cpu);
  uint before = total_ticks(e);
  pause(105);  // more than two 50-tick boost intervals
  e = lookup(t, cpu);
  uint gained = total_ticks(e) - before;

  kill(cpu);
  for (int k = 0; k < 2 * NPAIRS; k++)
    kill(kids[k]);
  for (int k = 0; k < 2 * NPAIRS + 1; k++)
    wait(0);

#if MLFQ_BOOST_INTERVAL > 0
  if (gained < 2)
    fail(t, "CPU-bound process starved despite the boost");
#elif !MLFQ_ACCOUNT_ACROSS_YIELDS
  if (gained > 1)
    fail(t, "CPU-bound process ran although nothing boosts it");
#else
  // With accounting across yields the ping-pong processes are charged
  // for all the CPU they use and are demoted too, so they cannot
  // starve the CPU-bound process; nothing to check either way.
#endif
  printf("mlfqtest: %s OK (CPU-bound process got %d of 105 ticks)\n",
         t, gained);
}

static void
test_starve(void)
{
  run_in_child("starve", starve_body);
}

// Gaming: a process that runs for about 70% of a tick, then sleeps
// before the tick arrives, over and over. It is almost never running
// when the timer fires, so counting ticks would never charge it.
// With accounting across yields (MLFQ_ACCOUNT_ACROSS_YIELDS 1) its
// measured CPU time builds up and it is demoted; under the naive rule
// each sleep restarts its allotment and it stays at level 0.
#define GAME_ROUNDS 12

// Busy-loop for n iterations, checking the clock every 1000 like the
// calibration loop below, so an iteration costs the same in both.
static void
loop(uint n)
{
  for (uint i = 0; i < n; i++)
    if (i % 1000 == 0)
      uptime();
}

static void
gaming_body(void)
{
  char *t = "gaming";

  // Calibrate: how many iterations fit in one tick? Start just after
  // a tick and count until the next one.
  pause(1);
  int t0 = uptime();
  uint per_tick = 0;
  while (uptime() == t0)
    for (int k = 0; k < 1000; k++, per_tick++)
      if (k == 0)
        uptime();

  avoid_boost(40);
  int pid = fork();
  if (pid < 0)
    fail(t, "fork failed");
  if (pid == 0) {
    for (int r = 0; r < GAME_ROUNDS; r++) {
      pause(1);                    // wake just after a tick...
      loop(per_tick * 7 / 10);     // ...run 70% of it...
    }                              // ...and sleep before the next
    struct pstat_entry *e = lookup(t, getpid());
    printf("mlfqtest: %s: after %d rounds the gamer is at level %d, "
           "with %d timer ticks recorded\n",
           t, GAME_ROUNDS, e->level, total_ticks(e));
#if MLFQ_ACCOUNT_ACROSS_YIELDS
    if (e->level == 0)
      fail(t, "gaming process was not demoted");
#else
    if (e->level != 0)
      fail(t, "gaming process was demoted under the naive rule");
#endif
    exit(0);
  }
  int status;
  wait(&status);
  if (status != 0)
    exit(1);
  printf("mlfqtest: %s OK\n", t);
}

static void
test_gaming(void)
{
  run_in_child("gaming", gaming_body);
}

// A forked child starts at the top level without inheriting the
// parent's ticks, and its slot is cleared once it has been reaped.
static void
test_fork(void)
{
  char *t = "fork";
  int fds[2];
  char c;

  // Use up the parent's level-0 allotment first, so we can tell
  // whether the child inherits the parent's level.
  avoid_boost(8);
  spin(3);
  struct pstat_entry *me = lookup(t, getpid());
  if (me == 0 || me->level == 0)
    fail(t, "parent should have been demoted first");

  if (pipe(fds) < 0)
    fail(t, "pipe failed");
  int pid = fork();
  if (pid < 0)
    fail(t, "fork failed");
  if (pid == 0) {
    // Block until the parent has looked at us.
    close(fds[1]);
    read(fds[0], &c, 1);
    exit(0);
  }
  close(fds[0]);

  struct pstat_entry *e = lookup(t, pid);
  if (e == 0)
    fail(t, "child not in table");
  if (e->level != 0)
    fail(t, "child inherited the parent's level");
  // The child has barely run (it blocks at once in read), so it may
  // have caught at most one tick, never the parent's count.
  if (e->ticks_used > 1 || total_ticks(e) > 1)
    fail(t, "child inherited or gained ticks");

  write(fds[1], "x", 1);
  close(fds[1]);
  if (wait(0) != pid)
    fail(t, "wait returned wrong pid");
  if (lookup(t, pid) != 0)
    fail(t, "reaped child still in table");
  printf("mlfqtest: %s OK\n", t);
}

// A pointer outside the address space is rejected.
static void
test_badptr(void)
{
  char *t = "badptr";
  if (getpinfo((struct pstat *)0xffffffffffffffffULL) != -1)
    fail(t, "bad pointer accepted");
  printf("mlfqtest: %s OK\n", t);
}

int
main(int argc, char *argv[])
{
  test_self();
  test_unused();
  test_demote();
  test_iobound();
  test_sleepticks();
  test_rr();
  test_preempt();
  test_boost();
  test_starve();
  test_gaming();
  test_fork();
  test_badptr();
  printf("mlfqtest: OK\n");
  exit(0);
}
