// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)

  int nice;  //(pa1)

  
  uint runtime_d_weight; //(pa2)-ps output

  uint runtime; //(pa2)-ps output 총 런타임, 프로세스가 실제로 CPU를 사용한 시간
  uint vruntime; //(pa2)-ps output 가상 런타임
  uint total_tick; //(pa2)-ps output 프로세스가 실행된 총 tick 수, 이 값은 프로세스의 실행 빈도와 관련 있음 

  uint time_slice; //(pa2) scheduler() 안에서 계산하여 저장
  
  // int weight; //(pa2) 프로세스 가중치 scheduler()안에서 계산하여 사용
 


};


// nice value에 따른 weight hard coding 
const int weight_table[40] = {
 /* 0  */     88761,     71755,     56483,     46273,     36291,
 /* 5  */     29154,     23254,     18705,     14949,     11916,
 /* 10 */      9548,      7620,      6100,      4904,      3906,
 /* 15 */      3121,      2501,      1991,      1586,      1277,
 /* 20 */      1024,       820,       655,       526,       423,
 /* 25 */       335,       272,       215,       172,       137,
 /* 30 */       110,        87,        70,        56,        45,
 /* 35 */        36,        29,        23,        18,        15,
};



// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap


// pa1 수정 예상 사항 
/*
int pri  // priority if the process -> nice value, 이미 pa1에서 수정 완료 
int weight // weight baseed on priority of process
uint64 vruntime 
uint64 aruntime // actual runtime 

???
uint64 runtick //running tick 
uint64 timeslice // assigned time slice 
uint64 vnice // high-level vruntime? overflow 발생한 횟수 
???

uint64 starttime  // time when the process wakes up and execute
uint64 interval // the scheduler period for the process
*/