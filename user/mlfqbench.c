// mlfqbench: workloads for comparing schedulers (proposal step 6).
// Each prints one or more "RESULT ..." lines. All times are in ticks.
//
//   mlfqbench cpu N     N CPU-bound jobs started together
//   mlfqbench io N      N I/O-bound (interactive) jobs started together
//   mlfqbench mixed N   N CPU-bound and N I/O-bound jobs, interleaved
//       For each kind of job: average turnaround (fork to exit),
//       waiting (ticks spent RUNNABLE, from waitx()), response (fork
//       to first run) and the longest response.
//
//   mlfqbench starve    A CPU-bound process alongside two ping-pong
//       pairs that keep level 0 busy, for 300 ticks: how many ticks
//       it got and the longest gap between two of them.
//
//   mlfqbench game      A gaming process (runs about 70% of each tick,
//       sleeps before the next) against a CPU-bound process, for 200
//       ticks: how many ticks of CPU each got, as measured by the
//       kernel (getpinfo cputime), which tick sampling cannot see.

#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/pstat.h"
#include "user/user.h"
#include "user/uoa.h"

#define MAXJOBS 16
#define CPU_JOB 30   // CPU-bound job: this many million loop iterations
#define IO_ROUNDS 10 // I/O-bound job: this many rounds of a little work
                     // and a one-tick sleep (standing in for I/O)

static struct pstat ps;  // too big for the user stack

// Busy-loop for n iterations, checking the clock every 1000 (so the
// cost per iteration matches calibrate()).
static void
loop(uint n)
{
  for (uint i = 0; i < n; i++)
    if (i % 1000 == 0)
      uptime();
}

// How many loop() iterations fit in one tick, on an idle CPU?
static uint
calibrate(void)
{
  pause(1);
  int t0 = uptime();
  uint n = 0;
  while (uptime() == t0)
    for (int k = 0; k < 1000; k++, n++)
      if (k == 0)
        uptime();
  return n;
}

// Print x/10 with one decimal place (xv6's printf has no %f).
static void
print10(char *label, int x)
{
  printf(" %s=%d.%d", label, x / 10, x % 10);
}

static uint
cpu_ticks(int pid)
{
  if (getpinfo(&ps) < 0)
    return 0;
  for (int i = 0; i < NPROC; i++)
    if (ps.proc[i].state != 0 && ps.proc[i].pid == pid) {
      uint sum = 0;
      for (int l = 0; l < MLFQ_LEVELS; l++)
        sum += ps.proc[i].ticks[l];
      return sum;
    }
  return 0;
}

// This process's kernel-measured CPU time, in hundredths of a tick.
static uint
my_cputime(void)
{
  if (getpinfo(&ps) < 0)
    return 0;
  for (int i = 0; i < NPROC; i++)
    if (ps.proc[i].state != 0 && ps.proc[i].pid == getpid())
      return ps.proc[i].cputime;
  return 0;
}

// ---- cpu / io / mixed ----

struct job {
  int pid;
  int io;          // 1 = I/O-bound, 0 = CPU-bound
  int forked;      // uptime() before fork
  int started;     // uptime() when the child first ran
  int ended;       // uptime() when wait returned it
  uint runnable;   // from waitx()
};

static void
run_job(int io)
{
  if (io) {
    for (int r = 0; r < IO_ROUNDS; r++) {
      loop(20000);
      pause(1);
    }
  } else {
    for (int m = 0; m < CPU_JOB; m++)
      loop(1000000);
  }
  exit(0);
}

static void
report(char *workload, char *kind, struct job *jobs, int n, int io)
{
  int k = 0, turn = 0, wait = 0, resp = 0, maxresp = 0;
  for (int i = 0; i < n; i++) {
    if (jobs[i].io != io)
      continue;
    k++;
    turn += jobs[i].ended - jobs[i].forked;
    wait += jobs[i].runnable;
    int r = jobs[i].started - jobs[i].forked;
    resp += r;
    if (r > maxresp)
      maxresp = r;
  }
  if (k == 0)
    return;
  printf("RESULT workload=%s kind=%s n=%d", workload, kind, k);
  print10("turnaround", turn * 10 / k);
  print10("waiting", wait * 10 / k);
  print10("response", resp * 10 / k);
  printf(" maxresponse=%d\n", maxresp);
}

static void
bench(char *workload, int ncpu, int nio)
{
  struct job jobs[MAXJOBS];
  int n = 0, fds[2];

  if (pipe(fds) < 0) {
    printf("mlfqbench: pipe failed\n");
    exit(1);
  }
  // Interleave the two kinds so neither always starts first.
  for (int i = 0; i < ncpu || i < nio; i++) {
    for (int io = 0; io <= 1; io++) {
      if ((io && i >= nio) || (!io && i >= ncpu))
        continue;
      jobs[n].io = io;
      jobs[n].forked = uptime();
      int pid = fork();
      if (pid < 0) {
        printf("mlfqbench: fork failed\n");
        exit(1);
      }
      if (pid == 0) {
        int msg[2] = {getpid(), uptime()};  // report first run
        write(fds[1], msg, sizeof(msg));
        run_job(io);
      }
      jobs[n++].pid = pid;
    }
  }
  close(fds[1]);

  for (int k = 0; k < n; k++) {
    uint runnable, sleeping;
    int pid = waitx(0, &runnable, &sleeping);
    int now = uptime();
    for (int i = 0; i < n; i++)
      if (jobs[i].pid == pid) {
        jobs[i].ended = now;
        jobs[i].runnable = runnable;
      }
  }
  int msg[2];
  while (read(fds[0], msg, sizeof(msg)) == sizeof(msg))
    for (int i = 0; i < n; i++)
      if (jobs[i].pid == msg[0])
        jobs[i].started = msg[1];

  report(workload, "cpu", jobs, n, 0);
  report(workload, "io", jobs, n, 1);
}

// ---- starve ----

static void
pingpong(int in, int out, int deadline)
{
  char c = 0;
  while (uptime() < deadline) {
    if (out >= 0)
      write(out, &c, 1);
    if (read(in, &c, 1) != 1)
      break;
    out = out < 0 ? -out - 1 : out;
  }
  exit(0);
}

static void
starve(void)
{
  int window = 300;
  int deadline = uptime() + window + 50;
  int kids[5], nk = 0;

  int cpu = fork();
  if (cpu == 0) {
    while (uptime() < deadline)
      loop(1000000);
    exit(0);
  }
  kids[nk++] = cpu;
  for (int k = 0; k < 2; k++) {
    int ab[2], ba[2];
    pipe(ab);
    pipe(ba);
    if ((kids[nk++] = fork()) == 0)
      pingpong(ba[0], ab[1], deadline);
    if ((kids[nk++] = fork()) == 0)
      pingpong(ab[0], -ba[1] - 1, deadline);
    close(ab[0]); close(ab[1]); close(ba[0]); close(ba[1]);
  }

  pause(10);  // let everything get going
  uint first = cpu_ticks(cpu), last = first;
  int gap = 0, maxgap = 0;
  for (int k = 0; k < window; k++) {
    pause(1);
    uint now = cpu_ticks(cpu);
    if (now == last) {
      gap++;
      if (gap > maxgap)
        maxgap = gap;
    } else {
      gap = 0;
    }
    last = now;
  }
  for (int k = 0; k < nk; k++)
    kill(kids[k]);
  for (int k = 0; k < nk; k++)
    wait(0);
  printf("RESULT starve window=%d cputicks=%d maxgap=%d\n",
         window, last - first, maxgap);
}

// ---- game ----

static void
game(void)
{
  int window = 200;
  uint per_tick = calibrate();
  int fds[2];
  pipe(fds);

  int start = uptime() + 5;
  int deadline = start + window;
  for (int who = 0; who < 2; who++) {
    if (fork() == 0) {
      while (uptime() < start)
        pause(1);
      uint before = my_cputime();
      if (who == 0) {             // CPU-bound
        while (uptime() < deadline)
          loop(per_tick / 10);
      } else {                    // gamer
        while (uptime() < deadline) {
          pause(1);
          loop(per_tick * 7 / 10);
        }
      }
      // CPU used in the window, in tenths of a tick.
      int msg[2] = {who, (int)((my_cputime() - before) / 10)};
      write(fds[1], msg, sizeof(msg));
      exit(0);
    }
  }
  close(fds[1]);
  wait(0);
  wait(0);
  // Each child's last piece of work may run a little past the
  // deadline, so report the time actually taken as well.
  int elapsed = uptime() - start;
  int got[2] = {0, 0}, msg[2];
  while (read(fds[0], msg, sizeof(msg)) == sizeof(msg))
    got[msg[0]] = msg[1];
  printf("RESULT game window=%d elapsed=%d", window, elapsed);
  print10("cpubound", got[0]);
  print10("gamer", got[1]);
  printf("\n");
}

// Print the configuration this program was built with, so every
// result can be traced to its settings.
static void
config(void)
{
  int q[] = MLFQ_QUANTA, al[] = MLFQ_ALLOTMENTS;
  printf("CONFIG sched=%s quanta=", SCHED_MLFQ ? "mlfq" : "rr");
  for (int l = 0; l < MLFQ_LEVELS; l++)
    printf(l ? ",%d" : "%d", q[l]);
  printf(" allotments=");
  for (int l = 0; l < MLFQ_LEVELS; l++)
    printf(l ? ",%d" : "%d", al[l]);
  printf(" boost=%d across=%d\n", MLFQ_BOOST_INTERVAL,
         MLFQ_ACCOUNT_ACROSS_YIELDS);
}

int
main(int argc, char *argv[])
{
  config();
  if (argc == 3 && strcmp(argv[1], "cpu") == 0) {
    bench("cpu", atoi(argv[2]), 0);
  } else if (argc == 3 && strcmp(argv[1], "io") == 0) {
    bench("io", 0, atoi(argv[2]));
  } else if (argc == 3 && strcmp(argv[1], "mixed") == 0) {
    bench("mixed", atoi(argv[2]), atoi(argv[2]));
  } else if (argc == 2 && strcmp(argv[1], "starve") == 0) {
    starve();
  } else if (argc == 2 && strcmp(argv[1], "game") == 0) {
    game();
  } else {
    printf("usage: mlfqbench cpu|io|mixed N  |  mlfqbench starve|game\n");
    exit(1);
  }
  exit(0);
}
