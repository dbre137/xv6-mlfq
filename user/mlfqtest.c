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

// CPU time is charged to the running process's current level.
// Nothing demotes yet, so it all lands on level 0.
static void
test_cputicks(void)
{
  char *t = "cputicks";
  struct pstat_entry *e = lookup(t, getpid());
  if (e == 0)
    fail(t, "own pid not in table");
  uint before = e->ticks[0];

  spin(10);

  e = lookup(t, getpid());
  uint gained = e->ticks[0] - before;
  // On one CPU with nothing else runnable, we should get almost
  // every one of the 10 ticks.
  if (gained < 8)
    fail(t, "fewer than 8 of 10 ticks counted at level 0");
  if (gained > 11)
    fail(t, "more ticks counted than elapsed");
  for (int l = 1; l < MLFQ_LEVELS; l++)
    if (e->ticks[l] != 0)
      fail(t, "ticks counted below level 0");
  if (e->ticks_used != e->ticks[0])
    fail(t, "ticks_used differs from level-0 ticks");
  printf("mlfqtest: %s OK (%d ticks at level 0)\n", t, gained);
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

// A forked child starts at the top level without inheriting the
// parent's ticks, and its slot is cleared once it has been reaped.
static void
test_fork(void)
{
  char *t = "fork";
  int fds[2];
  char c;

  struct pstat_entry *me = lookup(t, getpid());
  if (me == 0 || me->ticks[0] < 2)
    fail(t, "parent should have run for some ticks first");

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
    fail(t, "child not at level 0");
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
  test_cputicks();
  test_sleepticks();
  test_fork();
  test_badptr();
  printf("mlfqtest: OK\n");
  exit(0);
}
