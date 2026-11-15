// MLFQ statistics returned by the getpinfo() system call.
// Shared by the kernel and user programs; include param.h
// (kernel/param.h in user programs) before this file.

// One entry per process-table slot. state uses the values of
// enum procstate in proc.h (0 = UNUSED: the slot is free and
// every other field is zero).
struct pstat_entry {
  int pid;
  int state;
  char name[16];
  int level;                 // current MLFQ level, 0 = highest
  uint ticks_used;           // ticks used at the current level
  uint ticks[MLFQ_LEVELS];   // ticks run at each level since creation
};

struct pstat {
  struct pstat_entry proc[NPROC];
};
