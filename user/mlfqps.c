// mlfqps: show every process's MLFQ level and CPU ticks per level.
//
//   mlfqps                   print the table once
//   mlfqps interval count    print it count times, interval ticks apart
//
// For example, run a workload in the background and watch processes
// move between levels:  mlfqbench mixed 3 &  then  mlfqps 10 8

#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/pstat.h"
#include "user/user.h"

// Too big for the one-page user stack, so keep it global.
static struct pstat ps;

static char *states[] = {"unused", "used", "sleep", "runble", "run", "zombie"};

// xv6's printf has no field widths, so pad columns by hand.
static void
pad(char *str, int width)
{
  for (int n = strlen(str); n < width; n++)
    printf(" ");
  printf("%s", str);
}

static void
padnum(int v, int width)
{
  char buf[12];
  int i = sizeof(buf) - 1;
  buf[i] = 0;
  do {
    buf[--i] = '0' + v % 10;
    v /= 10;
  } while (v > 0 && i > 0);
  pad(&buf[i], width);
}

static void
show(void)
{
  if (getpinfo(&ps) < 0) {
    printf("mlfqps: getpinfo failed\n");
    exit(1);
  }
  printf("tick %d\n", uptime());
  printf("  PID STATE  LVL  TICKS/LEVEL      NAME\n");
  for (int i = 0; i < NPROC; i++) {
    struct pstat_entry *e = &ps.proc[i];
    if (e->state == 0)
      continue;
    char *st = (e->state >= 0 && e->state < 6) ? states[e->state] : "?";
    padnum(e->pid, 5);
    printf(" %s", st);
    pad("", 6 - strlen(st));
    padnum(e->level, 4);
    printf(" ");
    for (int l = 0; l < MLFQ_LEVELS; l++)
      padnum(e->ticks[l], 5);
    printf("  %s\n", e->name);
  }
}

int
main(int argc, char *argv[])
{
  int interval = 0, count = 1;

  if (argc == 3) {
    interval = atoi(argv[1]);
    count = atoi(argv[2]);
  } else if (argc != 1) {
    printf("usage: mlfqps [interval count]\n");
    exit(1);
  }
  for (int k = 0; k < count; k++) {
    if (k > 0)
      pause(interval);
    show();
  }
  exit(0);
}
