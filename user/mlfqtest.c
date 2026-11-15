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

// A forked child starts at the top level with no ticks, and its
// slot is cleared once it has exited and been reaped.
static void
test_fork(void)
{
  char *t = "fork";
  int fds[2];
  char c;

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
  if (e->ticks_used != 0)
    fail(t, "child has ticks used");
  for (int l = 0; l < MLFQ_LEVELS; l++)
    if (e->ticks[l] != 0)
      fail(t, "child has recorded ticks");

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
  test_fork();
  test_badptr();
  printf("mlfqtest: OK\n");
  exit(0);
}
