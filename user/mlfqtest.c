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

// A CPU-bound process is charged one tick at a time at its current
// level and is demoted once it has used that level's allotment:
// exactly 2 ticks at level 0, then 4 at level 1, then it stays at
// the lowest level (allotments {2, 4, 0} in kernel/param.h).
static void
test_demote(void)
{
  char *t = "demote";
  int pid = fork();
  if (pid < 0)
    fail(t, "fork failed");
  if (pid == 0) {
    spin(10);
    struct pstat_entry *e = lookup(t, getpid());
    uint total = total_ticks(e);
    printf("mlfqtest: %s: level %d, ticks %d/%d/%d\n", t, e->level,
           e->ticks[0], e->ticks[1], e->ticks[2]);
    if (e->ticks[0] != 2 || e->ticks[1] != 4)
      fail(t, "expected 2 ticks at level 0 and 4 at level 1");
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
  test_fork();
  test_badptr();
  printf("mlfqtest: OK\n");
  exit(0);
}
