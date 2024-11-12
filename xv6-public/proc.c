#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"  // PGSIZE 4096  PTE_U 0x004
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

#include "sleeplock.h" //??
#include "fs.h" //??
#include "file.h"



#define MMAPBASE 0x40000000 //pa3

// • Manage all mmap areas created by each mmap() call in one mmap_area array.
// • Maximum number of mmap_area array is 64.
struct mmap_area marea[64] = {0};

extern int freememCount();


struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

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
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

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
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
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

//pa3) fork()는 부모와 완전히 동일한 메모리 콘텐츠를 갖는 자식을 생성 
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){ //pa3) allocproc() 함수를 통해 kernel stack을 할당
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){ //pa3) copyuvm()을 통해 부모의 page table을 복사 
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

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  for (int i = 0; i < 64 ; i++){
    if((marea[i].isUsed == 1) && (marea[i].p == curproc)){
      for (int t = 0; t < 64; t++){
        if(marea[t].isUsed == 0){
          marea[t].isUsed = marea[i].isUsed;
          marea[t].f = marea[i].f; 
          marea[t].addr = marea[i].addr;
          marea[t].length = marea[i].length; 
          marea[t].offset = marea[i].offset; 
          marea[t].prot = marea[i].prot; 
          marea[t].flags = marea[i].flags;
          marea[t].p = np;

          uint start_addr = marea[i].addr; 
          uint end_addr = start_addr + marea[i].length;

          for (uint va = start_addr; va < end_addr; va += PGSIZE){
            pte_t *pte = walkpgdir(curproc->pgdir, (void*)va, 0); 
            if (pte && (*pte & PTE_P)){
              char *mem = kalloc(); 
              if (!mem){ // kalloc() 실패 
                return 0; 
              }
              memset(mem, 0, PGSIZE);
              memmove(mem, (void*)va, PGSIZE);
              int ifFail = mappages(curproc->pgdir, (void *)va, PGSIZE, V2P(mem), prot|PTE_U); // perm에 사용자 권한 추가
              if (ifFail == -1){ // mappages() 실패
                kfree(mem);
                return 0; 
              } 
            }
          }
          break;
        }
      }
    }
  }

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
wait(void)
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
        return pid;
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
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
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
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

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

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
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



//pa3) mmap() system call on xv6

uint mmap(uint addr, int length, int prot, int flags, int fd, int offset){
  struct proc *curproc = myproc();

  uint start_addr = MMAPBASE + addr;
  uint end_addr = start_addr + length;

  // 4. flags can be given with the combinations / 2가지 플래그 있음 
  // 1) If MAP_ANONYMOUS is given, it is anonymous mapping / mapped memory는 0으로 채워짐
  // 2) If MAP_ANONYMOUS is not given, it is file mapping / file은 주어진 fd, 즉 file descripter를 통해 매핑되고 mapped memory는 파일의 내용물을 포함하게 됨
  struct file *file = (flags & MAP_ANONYMOUS) ? 0 : curproc->ofile[fd]; // ??


  if (length <= 0 || length % PGSIZE != 0){
    return 0; // fail if length is invalid
  }

  // - It's not anonymous, but when the fd is -1
  if (!(flags & MAP_ANONYMOUS) && fd == -1){
    return 0; 
  }

  // - The protection of the file and the prot of the parameter are different
  if (file && ((prot & PROT_READ) && !(file->readable))){ // ?? 오류 원인 찾아야 함 
    return 0; 
  }

  if (file && ((prot & PROT_WRITE) && !(file->writable))){ // ?? 오류 원인 찾아야 함 
    return 0; 
  } 

  if (file){
    file = filedup(file);
  }

  // find unused mmap_area & 할당 
  for (int i = 0; i < 64; i++){
    if (marea[i].isUsed == 0){
      marea[i].isUsed = 1; 
      marea[i].f = file; // ?? 확인 필요 
      marea[i].addr = start_addr; 
      marea[i].length = length; 
      marea[i].offset = offset; 
      marea[i].prot = prot; 
      marea[i].flags = flags;
      marea[i].p = curproc; 
      break;
    }
  }
  
  // If MAP_POPULATE is not given, just record its mapping area. 
  // -> page fault 발생하면 allocate physical page & make page table to according page

  // 3) If MAP_POPULATE is given, allocate physical page & make page table for whole mapping area.
  // 4) If MAP_POPULATE is not given, just record its mapping area.
  if (flags & MAP_POPULATE){
    for (uint va = start_addr; va < end_addr; va+= PGSIZE){
      char *mem = kalloc(); 
      if (!mem){ // kalloc() 실패 
        return 0; 
      }
      memset(mem, 0, PGSIZE);

      if (!(flags & MAP_ANONYMOUS)){
        file->off = offset; 
        fileread(file, mem, PGSIZE); // 실제 파일 값을 페이지 단위로 읽어 들여 mem 에 저장 
      }

      // intmappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
      // 페이지 디렉터리 포인터 pgdir, 시작 가상 주소 va, 매핑할 바이트 크기 size, 시작 물리 주소 pa, 페이지 권한 perm
      int ifFail = mappages(curproc->pgdir, (void *)va, PGSIZE, V2P(mem), prot|PTE_U); // perm에 사용자 권한 추가
      if (ifFail == -1){ // mappages() 실패
        kfree(mem);
        return 0; 
      }
      // mappages (1760) installs mappings into a page table for a range of virtual addresses to a corresponding range of physical addresses
      // mappages 함수는 주어진 가상 주소(virtual address) 범위에 대해 페이지 테이블 항목(Page Table Entry, PTE)을 생성하여 가상 주소에서 시작하는 메모리 영역을 물리 주소(physical address)와 매핑

    }  
  }
  return start_addr;


//= 1. addr is always page-aligned (0, 4096, 8192) 프로세스의 virtual memory address 중 어디가 mmap의 시작주소가 될지 결정
//= - MMAPBASE + addr is the start address of mapping  / mmap의 시작주소 계산
//= - MMAPBASE of each process’s virtual address is 0x40000000

//= 2. length is also a multiple of page size / mmap에 얼마만큼의 크기를 매핑할지 결정 
//= - MMAPBASE + addr + length is the end address of mapping

// 3. prot can be PROT_READ or PROT_READ|PROT_WRITE / mmap된 메모리가 read 또는 write될 수 있는지 결정 
// - prot should be match with file’s open flag

//= 4. flags can be given with the combinations / 2가지 플래그 있음 
//= 1) If MAP_ANONYMOUS is given, it is anonymous mapping / mapped memory는 0으로 채워짐
//= 2) If MAP_ANONYMOUS is not given, it is file mapping / file은 주어진 fd, 즉 file descripter를 통해 매핑되고 mapped memory는 파일의 내용물을 포함하게 됨

// 당장 할당할지, 나중에 할당할지
// 3) If MAP_POPULATE is given, allocate physical page & make
// page table for whole mapping area.
// 4) If MAP_POPULATE is not given, just record its mapping
// area.
// If page fault occurs to according area (access to mapping area’s virtual
// address), allocate physical page & make page table to according page
// 5) Other flags will not be used

// 5. fd is given for file mappings, if not, it should be -1

// 6. offset is given for file mappings, if not, it should be 0 / file의 어디에서부터 매핑을 시작할 지 결정하는 데 사용되는 값

//= Return
//= Succeed: return the start address of mapping area
// Failed: return 0
//= - It's not anonymous, but when the fd is -1
//= - The protection of the file and the prot of the parameter are different
// - The situation in which the mapping area is overlapped is not considered
// - If additional errors occur, we will let you know by writing notification



}

// How file mmap() works 
// 1) Private file mapping with MAP_POPULATE
// • mmap(0, 8192, PROT_READ, MAP_POPULATE, fd, 4096)
// – mmap 2pages

// virtual 메모리에 메모리 할당 후 physical memory에서 두 개의 페이지를 받음 
// 4096 오프셋을 적용한 파일 위치에서 두 개의 페이지를 읽어옴
// 마지막으로 페이지 테이블을 통해 매핑 


// 2) Private file mapping without MAP_POPULATE
// • mmap(0, 8192, PROT_READ, 0, fd, 4096)
// – mmap 2pages

// 단순히 virtual memory에 메모리 할당하면 mmap() 종료 
// 만약 프로세스가 mmapped region에 접근하면 page fault trap이 발생 
// 접근한 영역과 대응되는 엔트리가 page table에 없기 때문

// 이 경우 page fault handler가 physical memory를 할당하고 페이지 테이블을 활성화기킴 


// 3) Private anonymous mapping with MAP_POPULATE
// • mmap(0, 8192, PROT_READ,
// MAP_POPULATE|MAP_ANONYMOUS, -1, 0)
// – mmap 2pages

// • Mostly same, but allocate page filled with 0

// 메모리 영역을 파일로부터 읽어오는 것이 아닌 0으로 초기화 



//pa3
// 2. Page Fault Handler on xv6

// Page fault handler is for dealing with access on mapping region with physical page & page table is not allocated
// • Succeed: Physical pages and page table entries are created normally, and the process works
// without any problems
// • Failed: The process is terminated

int page_fault_handler(struct trapframe *tf){
  struct proc *curproc = myproc();

  // 2. In page fault handler, determine fault address by reading CR2 register(using rcr2()) 
  // & access was read or write
  uint fault_addr = rcr2(); // CR2 레지스터에서 페이지 폴트 주소 읽기

  uint rounded_addr = PGROUNDDOWN(fault_addr); // ?? 페이지 경계로 주소 정렬

  int isWrite = (tf->err&2) ? 1 : 0; 
  // write: tf->err&2 == 1 / read: tf->err&2 == 0  


  // 3. Find according mapping region in mmap_area
  struct mmap_area *mmap = 0; // mmap_area 구조체 포인터 초기화
  for (int i = 0; i < 64; i++){
    if (marea[i].isUsed == 1 && marea[i].p == curproc){ // 사용 중인지 확인 
      if (marea[i].addr <= rounded_addr && rounded_addr < (marea[i].addr + marea[i].length)){
        mmap = &marea[i]; 
        break; 
      }
    }
  }

  if (!mmap){ // If faulted address has no corresponding mmap_area, return -1
    return -1; 
  }

  if (isWrite && !(mmap->prot & PROT_WRITE)){ // 4. If fault was write while mmap_area is write prohibited, then return -1
    return -1; 
  }

  // cprintf('\npage fault ... %x\n', rounded_addr);  // ??

  /*
  uint start_addr = mmap->addr; 
  uint end_addr = start_addr + mmap->length; 

  for (uint va = start_addr; va < end_addr; va+= PGSIZE){
    if ((va <= rounded_addr) && (rounded_addr < va + PGSIZE)){
      char *mem = kalloc(); 
      if(!mem){
        return 0; // kalloc() 실패 
      }
      memset(mem, 0, PGSIZE);

      if (!(mmap->flags & MAP_ANONYMOUS)){
        struct file *file = mmap->f; 
        fileread(file, mem, PGSIZE); 

        int perm = mmap->prot|PTE_U; 
        if (isWrite){
          perm = perm|PTE_W;
        }
        int ifFail = mappages(curproc->pgdir, (void *)rounded_addr, PGSIZE, V2P(mem), perm); 
        if (ifFail == -1){ // mappages() 실패
          kfree(mem);
          return 0; 
        }

        file->off += PGSIZE;
      }

      else{
        int perm = mmap->prot|PTE_U; 
        if (isWrite){
          perm = perm|PTE_W;
        }
        int ifFail = mappages(curproc->pgdir, (void *)rounded_addr, PGSIZE, V2P(mem), perm); 
        if (ifFail == -1){ // mappages() 실패
          kfree(mem);
          return 0; 
        }
      }
    }

    return 0; 

  }

  */ 

  char *mem = kalloc();
  if (!mem) return -1;
  memset(mem, 0, PGSIZE); // Zero-out the new page

  if (!(mmap->flags & MAP_ANONYMOUS)) {
    struct file *file = mmap->f;
    file->off = mmap->offset;
    fileread(file, mem, PGSIZE);
    mmap->offset += PGSIZE; // Move file offset
  }

  int perm = mmap->prot | PTE_U;
  if (isWrite) perm |= PTE_W;

  if (mappages(curproc->pgdir, (void *)rounded_addr, PGSIZE, V2P(mem), perm) < 0) {
    kfree(mem);
    return -1;
  }

  return 0;

}
// 1. When an access occurs (read/write), catch according page fault (interrupt 14, T_PGFLT) in
// traps.h

// 2. In page fault handler, determine fault address by reading CR2 register(using rcr2()) & access
// was read or write
// read: tf->err&2 == 0 / write: tf->err&2 == 1

// 3. Find according mapping region in mmap_area
// If faulted address has no corresponding mmap_area, return -1

// 4. If fault was write while mmap_area is write prohibited, then return -1

// 5. For only one page according to faulted address
  // 1. Allocate new physical page
  // 2. Fill new page with 0
  // 3. If it is file mapping, read file into the physical page with offset
  // 4. If it is anonymous mapping, just left the page which is filled with 0s
  // 5. Make page table & fill it properly (if it was PROT_WRITE, PTE_W should be 1 in PTE value) // ??


// 3. munmap() system call on xv6
// - Unmaps corresponding mapping area
// - Return value: 1(succeed), -1(failed)

  // 1. addr will be always given with the start address of mapping region, which is page
  // aligned
  // 2. munmap() should remove corresponding mmap_area structure
  // If there is no mmap_area of process starting with the address, return -1
  // 3. If physical page is allocated & page table is constructed, should free physical page
  // & page table
  // When freeing the physical page should fill with 1 and put it back to freelist
  // 4. If physical page is not allocated (page fault has not been occurred on that
  // address), just remove mmap_area structure.
  // 5. Notice) In one mmap_area, situation of some of pages are allocated and some
  // are not can happen.
int munmap(uint addr){
  struct proc *curproc = myproc();

  struct mmap_area *mmap = 0; 
  for (int i = 0; i < 64; i++){
    if (marea[i].isUsed && (marea[i].addr == addr)){
      mmap = &marea[i];
      break;
    }
  }

  if (!mmap){ // If there is no mmap_area of process starting with the address, return -1
    return -1; 
  }

  // 3. If physical page is allocated & page table is constructed, 
  // should free physical page & page table
  
  uint start_addr = mmap->addr; 
  uint end_addr = start_addr + mmap->length;

  for (uint va = start_addr; va < end_addr; va += PGSIZE){
    pte_t *pte = walkpgdir(curproc->pgdir, (void*)va, 0); 
    if (pte && (*pte & PTE_P)){
      char *pa = P2V(PTE_ADDR(*pte));
      // memset(physical_page, 1, PGSIZE); // ??
      kfree(pa); 
      *pte = 0; 
    }
  }
  mmap->isUsed = 0; 
  if(mmap->f) fileclose(mmap->f); // ??
  mmap->addr = 0; 
  mmap->length = 0; 
  mmap->offset = 0; 
  mmap->prot = 0; 
  mmap->flags = 0; 
  mmap->p = 0; 

  return 1; 
}


// 4. freemem() system call on xv6
// - syscall to return current number of free memory
// pages
  // 1. When kernel frees (put page into free list),
  // freemem should be increase
  // 2. When kernel allocates (takes page from free list
  // and give it to process), freemem should decrease
int freemem(){
  return freememCount(); 
}  
