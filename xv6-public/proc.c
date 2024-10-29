#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h" 

int weight_table[40] = {
 /* 0  */     88761,     71755,     56483,     46273,     36291,
 /* 5  */     29154,     23254,     18705,     14949,     11916,
 /* 10 */      9548,      7620,      6100,      4904,      3906,
 /* 15 */      3121,      2501,      1991,      1586,      1277,
 /* 20 */      1024,       820,       655,       526,       423,
 /* 25 */       335,       272,       215,       172,       137,
 /* 30 */       110,        87,        70,        56,        45,
 /* 35 */        36,        29,        23,        18,        15,
};


struct {
  struct spinlock lock; //프로세스 테이블 접근 시 동시성 문제 방지 위한 스핀락
  struct proc proc[NPROC]; //프로세스 배열(최대프로세스 수)
} ptable; //운영체제가 관리하는 프로세스 테이블 

static struct proc *initproc; //초기 사용자 프로세스 

int nextpid = 1; //새 프로세스에 할당할 고유 pid 

//프로세스 제어에 사용되는 외부 함수들 
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);


//프로세스 테이블 접근 시 사용할 락 초기화 
void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}


// 현재 CPU의 ID를 반환, 다중 CPU 시스템에서 CPU간 스케줄링 관리를 위헤 필요
// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}


// 현재 실행 중인 CPU를 반환. 
// lapicid를 사용해 APIC ID로 CPU를 찾고, 실패 시 panic을 호출해 에러를 보고 
// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void) //새로운 프로세스 할당 
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED) //UNUSED 상태인 프로세스 찾고 
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO; //EMBRYO 상태로 변경(초기화)
  p->pid = nextpid++;
  p->nice = 20;  // 기본 nice 값은 20으로 초기화

  p->runtime_d_weight = 0; //(pa2)-ps output 
  p->runtime = 0; //(pa2)-ps output 총 런타임, 프로세스가 실제로 CPU를 사용한 시간
  p->vruntime = 0; //(pa2)-ps output 가상 런타임

  p->time_slice = 0; 
  

  //p->total_tick = 0; //(pa2)-ps output 프로세스가 실행된 총 tick 수, 이 값은 프로세스의 실행 빈도와 관련 있음 

  release(&ptable.lock);

  // Allocate kernel stack. 
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret; //준비된 상태에서 forkret함수 실행

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
// 첫 번째 사용자 프로세스를 설정 
void
userinit(void)  
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();  // 프로세스 할당 
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void) //부모프로세스(curproc)의 상태를 복사해 새로운 자식프로세스(np)를 생성하는 fork함수
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  //(pa2)
  // 자식 프로세스가 부모 프로세스의 runtime, vruntime, nice_value를 상속받도록 수정
  np->runtime = curproc->runtime; // 부모의 runtime 상속
  np->vruntime = curproc->vruntime; // 부모의 vruntime 상속
  np->nice = curproc->nice; // 부모의 nice value 상속


  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void) //부모프로세스가 자식 프로세스의 종료를 기다림
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid; //종료된 자식 프로세스를 찾으면 그 메모리를 해제하고 자식의 pid 반환
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void) // 시스템 스케줄러, 무한 루프를 돌며 실행 가능한 프로세스를 찾아 CPU에서 실행 
{
  struct proc *p;  // 프로세서를 가리키는 포인터 
  struct cpu *c = mycpu();  // 현재 CPU
  c->proc = 0;  // 현재 CPU에서 실행 중인 프로세스를 초기화 

  for(;;){ // 무한 루프 시작 -> 스케줄러가 계속해서 프로세스를 찾고 실행하도록 함 
    // Enable interrupts on this processor.
    sti();  // 현재 CPU에서 인터럽트를 활성화 

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);  // 프로세스 테이블을 순회하기 위해 ptable.lock을 획득


    int total_weight = 0; // 총 weight 값을 초기화 (total weight of runqueue)
    // ptable 순회하며 total_weight 계산
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){ 
      if(p->state == RUNNABLE) { // 실행 가능한 프로세스에 대해
        total_weight += weight_table[p->nice]; // 프로세스의 weight를 누적
      }
    }

    if(total_weight == 0) { // 모든 프로세스가 RUNNABLE 상태가 아닌 경우
      release(&ptable.lock);
      continue; // 다음 반복으로 넘어가서 다시 시도
    }

    struct proc *min_vrun_process = 0; // 가장 작은 vruntime을 가진 프로세스 포인터 초기화
    uint min_vruntime = ~0; // 최대 값으로 초기화하여 최소 vruntime을 찾기 쉽게 함 ~0 == 0xFFFFFFFF

    // - Select process with minimum virtual runtime from runnable processes
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){ 
      if(p->state == RUNNABLE && p->vruntime < min_vruntime) { // 실행 가능한 프로세스에 대해
          min_vruntime = p->vruntime; // 최소 vruntime 업데이트
          min_vrun_process = p; // 가장 작은 vruntime을 가진 프로세스 저장
      }
    }

    // 가장 작은 vruntime을 가진 프로세스가 발견되었다면 실행
    if(min_vrun_process) { // time_slice 계산
      min_vrun_process->time_slice = (10* 1000 * weight_table[min_vrun_process->nice] + total_weight - 1) / total_weight;


      // 스케줄링을 위한 프로세스 준비
      c->proc = min_vrun_process; 
      switchuvm(min_vrun_process); // 프로세스의 주소 공간으로 전환 
      min_vrun_process->state = RUNNING; // 프로세스의 상태를 RUNNING으로 변경 

      // 프로세스 실행
      swtch(&(c->scheduler), min_vrun_process->context);  
      switchkvm(); // 커널 가상 메모리 공간으로 전환

      // 현재 CPU에서 실행 중인 프로세스를 초기화
      c->proc = 0;  
    }

    release(&ptable.lock); // 프로세스 테이블의 잠금 해제
  }

  /* 기존 코드
  for(;;){ // 무한 루프 시작 -> 스케줄러가 계속해서 프로세스를 찾고 실행하도록 함 
    // Enable interrupts on this processor.
    sti();  // 현재 CPU에서 인터럽트를 활성화 

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);  // 프로세스 테이블을 순회하기 위해 ptable.lock을 획득 
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){ 
    // 프로세스 테이블의 각 프로세스를 순회하기 위해 포인터 p를 초기화
    // NPROC는 프로세스의 최대 수를 나타냄 

      if(p->state != RUNNABLE) 
        continue;
      // 현재 프로세스 p의 상태가 RUNNABLE(실행 가능 상태)가 아닌 경우, 다음 프로세스로 넘어감


      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;  // 현재 CPU에서 실행할 프로세스를 p로 설정 
      switchuvm(p); //  프로세스 p의 주소 공간으로 전환 -> 프로세스가 자신의 memory 공간 사용할 수 있도록 함 
      p->state = RUNNING;  // 프로세스의 상태를 RUNNING으로 변경 

      swtch(&(c->scheduler), p->context);  // CPU scheduler 컨텍스트에서 프로세스 p의 컨텍스트로 변경 -> CPU가 프로세스 실행하도록
      switchkvm(); // 커널 가상 메모리 공간으로 전환, 프로세스가 실행을 마치고 커널 모드로 돌아올 때 호출됨

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;  // 현재 CPU에서 실행 중인 프로세스를 초기화 (현재 CPU가 어떤 프로세스도 실행하고 있지 않음을 나타냄)
    }
  
    release(&ptable.lock); // 프로세스 테이블의 잠금 해제 -> 다른 스케줄러나 프로세스가 프로세스 테이블에 접근 가능 
  }
  */
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void) // 현재 실행 중인 프로세스를 안전하게 중단하고 다른 프로세스가 실행되도록 스케줄링
{
  int intena; // 인터럽트 활성화 상태를 저장하는 변수
  struct proc *p = myproc();
  // p는 현재 실행 중인 프로세스를 가리키는 포인터
  // myproc() 함수는 현재 프로세스의 포인터를 반환합니다.

  if(!holding(&ptable.lock))    // 프로세스 테이블의 잠금을 유지하고 있지 않으면 panic 상태 에 빠짐
    panic("sched ptable.lock"); // (스케줄링 과정에서 프로세스 테이블에 대한 동시 접근을 방지하기 위한 안전장치)
  if(mycpu()->ncli != 1) // ncli는 인터럽트 잠금 수준을 나타냄, 이 값이 1이 아니면 스케줄링 과정에서 잠금이 올바르지 않음을 나타냄
    panic("sched locks"); // 현재 CPU에서 커널 인터럽트가 활성화된 상태에서 호출되면 패닉 상태에 빠짐
  if(p->state == RUNNING) //현재 프로세스 p의 상태가 RUNNING인 경우 패닉 상태에 빠짐
    panic("sched running"); // 스케줄링 수행하는 동안 현재 프로세스가 실행 중일 수 없기 때문
  if(readeflags()&FL_IF) // 현재 CPU의 플래그 레지스터에서 인터럽트가 활성화된 경우 패닉 상태로
    panic("sched interruptible"); // 스케줄링이 진행되는 동안 인터럽트가 활성화되어서는 안됨
  intena = mycpu()->intena; // 현재 CPU의 인터럽트 활성화 상태를 intena 변수에 저장, 나중에 스케줄링이 끝난 후 원래 상태로 복원하기 위해 필요
  swtch(&p->context, mycpu()->scheduler); // swtch 함수를 호출하여 현재 프로세스의 컨텍스트에서 CPU 스케줄러의 컨텍스트로 전환
  mycpu()->intena = intena; // 이전에 저장한 인터럽트 활성화 상태를 복원. 이제 스케줄링이 완료된 후 프로세스의 실행 상태가 이전과 같게 됨
}

// Give up the CPU for one scheduling round.
void
yield(void) // 현재 실행 중인 프로세스가 CPU를 양보하고, 다른 프로세스가 실행될 수 있도록 스케줄러를 호출하는 역할
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space. 
// 이 함수가 사용자 공간으로 "복귀"하는 역할을 한다는 점에 주목
void
forkret(void) // fork 호출로 생성된 자식 프로세스가 처음으로 스케줄링될 때 호출됨
{
  static int first = 1; // forkret 함수가 처음 호출되었는지를 나타내는 플래그
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock); // 이 잠금은 스케줄러에서 이미 획득된 상태이므로, 현재 프로세스의 상태를 업데이트하고 다른 프로세스가 프로세스 테이블에 접근할 수 있도록 해줌

  if (first) {  // first 변수가 1인 경우, 즉 이 함수가 처음 호출될 때를 체크 
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0; // first 변수를 0으로 설정하여, 이후에 이 블록이 다시 실행되지 않도록 함, 즉 forkret이 호출될 때마다 초기화 코드를 한 번만 실행하도록
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
  // forkret 함수가 "호출자"로 돌아간다는 것을 설명
  // 실제로는 trapret으로 돌아가게 되며, 
  // 이는 프로세스가 사용자 공간으로 복귀하기 위한 과정
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc(); // 현재 실행 중인 프로세스를 가져옴
  
  if(p == 0)  // 현재 프로세스가 존재하지 않음
    panic("sleep");

  if(lk == 0) // 제공된 락 포인터(lk)가 NULL이면 오류
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk); 
    // 두 락이 동시에 유지되는 것 방지 
  }
  // Go to sleep.
  p->chan = chan; // 프로세스가 대기할 채널을 설정 
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0; // 프로세스가 깨어날 때 대기할 채널을 초기화. 이제 채널은 더 이상 유효하지 않으므로 NULL로 설정

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;
  int min_vrun = 0; // minimum vruntime 초기화
  int is_run = 0;
  int vrun_per_tick = 0;


  // RUNNABLE 프로세스가 있는지 확인하고 최소 vruntime 찾기
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if (p->state == RUNNABLE) {
      is_run = 1;
      min_vrun = p->vruntime;
    }
  }

  if(is_run == 1){
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state == RUNNABLE){
        if (min_vrun > p->vruntime){
          min_vrun = p->vruntime;
        }
      }
    }
  }
  
  //sleeping 프로세스의 vruntime 업데이트 
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == SLEEPING && p->chan == chan) {

      vrun_per_tick = (1000*1024)/(weight_table[p->nice]);
      if(min_vrun < vrun_per_tick){
        p->vruntime = 0;
      }
      else{
        p->vruntime = min_vrun - vrun_per_tick;
      }

      p->state = RUNNABLE;
    }
  }    
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}


//proj_1
//make three system calls
// default nice value is 20; 
// lower nice values cause more favorable scheduling

// it will be necessary to implement the nice value
// before creating the system call 

int getnice(int pid){                                   
  //optains the nice value of a process
  struct proc *p;

  acquire(&ptable.lock); //lock the process table 

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if (p->pid == pid){
      int nice_value = p->nice;
      release(&ptable.lock); //unlock the process table
      return nice_value; // return the nice value of target process on success
    }
  }

  release(&ptable.lock);
  return -1; // if there is no process corresponding to the pid
}


int setnice(int pid, int value){
  // check invalid value 
  // the range of valid nice value is 0~39
  if(value < 0 || value > 39){
    return -1;
  }

  struct proc *p;
  acquire(&ptable.lock); //lock the process table 

  //find the process with the given pid
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if (p->pid == pid){
      p->nice = value; //sets the nice value of a process
      release(&ptable.lock); //unlock the process table 
      return 0; // return 0 on success 
    }
  } 
  
  release(&ptable.lock);
  return -1; // if there is no process corresponding to the pid 

}



void ps(int pid){
  struct proc *p;
  acquire(&ptable.lock); //lock the process table

  cprintf("name\tpid\tstate\t\tpriority\truntime/weight\truntime\t\tvruntime\t\ttick %d\n", ticks*1000);
  

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if (pid == 0 || p->pid == pid){
      //enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };
      if(p->state == UNUSED){
         continue; 
      }

      char *state;
      switch(p->state){
        
        case EMBRYO: state = "EMBRYO"; break;
        case SLEEPING: state = "SLEEPING"; break;
        case RUNNABLE: state = "RUNNABLE"; break;
        case RUNNING: state = "RUNNING"; break;
        case ZOMBIE: state = "ZOMBIE"; break;
        default: state = "???"; break; 
      }

      cprintf("%s\t%d\t%s\t%d\t\t%d\t\t%d\t\t%d\n", p->name, p->pid, state, p->nice, p->runtime_d_weight, p->runtime, p->vruntime);
      
    }
    
  }
  release(&ptable.lock); //unlock the process table




  // prints out process(s)'s information 
  // -> name, pid, state, priority(nice value) of each process

 // if pid == 0, print out all process's information
 // otherwise, print out corresponding process's information
 // if there is no process corresponding to pid, print out nothing
 // no return value 

}
