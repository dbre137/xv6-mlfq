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

// MLFQ scheduler
#define SCHED_MLFQ   1     // 1 = MLFQ scheduler, 0 = xv6 round-robin baseline
#define MLFQ_LEVELS  3     // number of priority levels (0 = highest)
#define MLFQ_QUANTA      {1, 2, 4}  // per level: ticks a process runs before it yields
#define MLFQ_ALLOTMENTS  {2, 4, 0}  // per level: ticks used before demotion (0 = never)

