#define NPROC        64  // maximum number of processes
#define NCPU          8  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGBLOCKS    (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache
#define FSSIZE       2000  // size of file system in blocks
#define MAXPATH      128   // maximum file path name
#define USERSTACK    1     // user stack pages

// MLFQ scheduler. Each setting except MLFQ_LEVELS can be overridden
// at build time without editing this file, e.g.
//   make clean && make qemu MLFQ_DEFS='-DMLFQ_BOOST_INTERVAL=10'
// (run make clean first: make does not notice a changed MLFQ_DEFS).
#ifndef SCHED_MLFQ
#define SCHED_MLFQ          1          // 1 = MLFQ scheduler, 0 = xv6 round-robin baseline
#endif
#define MLFQ_LEVELS         3          // number of priority levels (0 = highest)
#ifndef MLFQ_QUANTA
#define MLFQ_QUANTA         {1, 2, 4}  // per level: ticks a process runs before it yields
#endif
#ifndef MLFQ_ALLOTMENTS
#define MLFQ_ALLOTMENTS     {2, 4, 0}  // per level: ticks used before demotion (0 = never)
#endif
#ifndef MLFQ_BOOST_INTERVAL
#define MLFQ_BOOST_INTERVAL 50         // ticks between priority boosts (0 = never boost)
#endif
#ifndef MLFQ_ACCOUNT_ACROSS_YIELDS
#define MLFQ_ACCOUNT_ACROSS_YIELDS 1   // 1 = CPU time used at a level carries over
                                       //     when a process sleeps; 0 = naive rule:
                                       //     sleeping restarts the allotment
#endif
#define TICK_INTERVAL 1000000      // timer (r_time) units per tick, about 0.1 s

