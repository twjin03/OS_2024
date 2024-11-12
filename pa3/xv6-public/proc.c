#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

#include "sleeplock.h" 
#include "fs.h" 
#include "file.h"

#define MMAPBASE 0x40000000 //pa3

struct mmap_area marea[64];

void init_marea(int i){
  marea[i].isUsed = 0; 
  marea[i].f = 0; 
  marea[i].addr = 0; 
  marea[i].length = 0; 
  marea[i].offset = 0; 
  marea[i].prot = 0; 
  marea[i].flags = 0;
  marea[i].p = 0; 
}

extern uint freememCount(void);


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
  for (int i = 0; i < 64; i++){
    init_marea(i);
  }
  
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
int
fork(void)
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
          pte_t *pte; 

          for (uint va = start_addr; va < end_addr; va += PGSIZE){
            pte = walkpgdir(curproc->pgdir, (void*)va, 0); 
            if(!pte) continue; 
            if(!(*pte & PTE_P)) continue; 

            int perm = marea[i].prot | PTE_U;
            if (*pte & PTE_W) perm |= PTE_W;
            if (mappages(np->pgdir, (void *)va, PGSIZE, PTE_ADDR(*pte), perm) == -1) {
              panic("fork: mappages failed during mmap area copy");
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

  // addr is always page-aligned 
  // length is also a multiple of page size 
  if (addr % PGSIZE != 0 || length % PGSIZE != 0){ // Address argument must be page aligned, if not, return 0
    return 0; 
  }

  struct proc *curproc = myproc();
  uint start_addr = MMAPBASE + addr;
  uint end_addr = start_addr + length;

  if (length <= 0 || length % PGSIZE != 0){
    return 0; // fail if length is invalid
  }

  // - It's not anonymous, but when the fd is -1
  if (!(flags & MAP_ANONYMOUS) && fd == -1){
    return 0; 
  }

  struct file *file = (flags & MAP_ANONYMOUS) ? 0 : curproc->ofile[fd]; // ??

  // Check file protection compatibility
  //prot can be PROT_READ or PROT_READ|PROT_WRITE
  //prot should be match with file's open flag
  if (file) {
    if ((prot & PROT_READ) && !(file->readable)) {
      return 0; // fail if file is not readable
    }
    if ((prot & PROT_WRITE) && !(file->writable)) {
      return 0; // fail if file is not writable
    }
    file = filedup(file); // duplicate file to keep it open
  }

  for (int i = 0; i < 64; i++){
    if (marea[i].isUsed == 0){
      marea[i].isUsed = 1; 
      marea[i].f = file; 
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

  // If MAP_POPULATE is given, 
  if (flags & MAP_POPULATE){
    for (uint va = start_addr; va < end_addr; va+= PGSIZE){
      char *mem = kalloc(); // 1) allocate physical page
      if (!mem){ // kalloc() fail
        return 0; 
      }
      memset(mem, 0, PGSIZE);

      if (!(flags & MAP_ANONYMOUS)){
        file->off = offset; 
        fileread(file, mem, PGSIZE); 
      }

      // 2) make page table for whole mapping area.
      int ifFail = mappages(curproc->pgdir, (void *)va, PGSIZE, V2P(mem), prot|PTE_U); 
      if (ifFail == -1){ 
        return 0; 
      }
    }  
  }
  return start_addr; // succeed 
}


// page fault handler 
// for dealing with access on mappig region 
// with physical page & page table is not allocated

// succed: physical page & page table entries are created normally 
// and process works without any problems 

// failed: the process is terminated
// -> refer. trap.c !!

int page_fault_handler(struct trapframe *tf){
  struct proc *curproc = myproc();

  // 2. In page fault handler, determine fault address by reading CR2 register(using rcr2()) 
  // & access was read or write
  uint fault_addr = rcr2(); 
  uint rounded_addr = PGROUNDDOWN(fault_addr);

  int isWrite = (tf->err&2) ? 1 : 0; 
  // write: tf->err&2 == 1 / read: tf->err&2 == 0  


  // 3. Find according mapping region in mmap_area
  struct mmap_area *mmap = 0; 
  for (int i = 0; i < 64; i++){
    if (marea[i].isUsed == 1 && marea[i].p == curproc){
      if (marea[i].addr <= rounded_addr && rounded_addr < (marea[i].addr + marea[i].length)){
        mmap = &marea[i]; 
        cprintf("Found mmap area: addr = 0x%x, length = 0x%x\n", mmap->addr, mmap->length);
        break; 
      }
    }
  }
  if (!mmap){ // If faulted address has no corresponding mmap_area, return -1
    cprintf("Error: No valid mmap_area for address 0x%x\n", fault_addr);
    curproc->killed = 1; // failed, terminate
    return -1; 
  }

  if (isWrite && !(mmap->prot & PROT_WRITE)){ // 4. If fault was write while mmap_area is write prohibited, then return -1
    cprintf("Error: Write access violation at address 0x%x (no write permission)\n", fault_addr);
    curproc->killed = 1; // failed, terminate
    return -1; 
  }

  // for only one page according to faulted adress
  char *mem = kalloc(); // allocate new physical page
  if (!mem) {
    cprintf("Error: Failed to allocate memory for page at address 0x%x\n", fault_addr);
    curproc->killed = 1; // failed, terminate
    return -1;
  }
  memset(mem, 0, PGSIZE); // fill new page with 0 

  // if it is file mapping,
  if (!(mmap->flags & MAP_ANONYMOUS)) { // read file into physical page with offset
    struct file *file = mmap->f;
    file->off = mmap->offset;
    fileread(file, mem, PGSIZE);
    file->off += PGSIZE; // Move file offset
  }
  // If it is anonymous mapping, just left the page which is filled with 0s

  int perm = mmap->prot | PTE_U;
  if (isWrite) perm = perm|PTE_W;

  // make page table & fill it properly (if it was PROT_WRITE, PTE_W should be 1 in PTE value)
  if (mappages(curproc->pgdir, (void *)rounded_addr, PGSIZE, V2P(mem), perm) < 0) {
    cprintf("mappings failed\n");
    curproc->killed = 1; // failed, terminate
    return -1;
  }
  return 0;
}


// unmaps corresponding mapping area: remove corresponding mmap_area structure
// return value: 1(succeed), -1(failed)
int munmap(uint addr){ // addr: start addr of mapping region, page aligned 

  if (addr % PGSIZE != 0) {
    return 0;
  } 

  struct proc *curproc = myproc();

  struct mmap_area *mmap = 0; 
  for (int i = 0; i < 64; i++){
    if(marea[i].addr == addr){
      if((marea[i].p = curproc)&&(marea[i].isUsed == 1)){
        mmap = &marea[i];
        break;
      }
    }
  }
  if (!mmap){ // If there is no mmap_area of process starting with the address, return -1
    return -1; 
  }

  // 3. If physical page is allocated & page table is constructed, 
  // should free physical page & page table
  uint start_addr = mmap->addr; 
  uint end_addr = start_addr + mmap->length;
  pte_t *pte; 

  for (uint va = start_addr; va < end_addr; va += PGSIZE){
    pte = walkpgdir(curproc->pgdir, (void*)va, 0); 
    if(!pte) continue; 
    if(!(*pte & PTE_P)) continue; 
    if (pte && (*pte & PTE_P)){ // if physical page is allocated & page table is constructed, 
      char *pa = P2V(PTE_ADDR(*pte));
      kfree(pa); 
      *pte = 0; 
      // should free physical page & page table
    }
  }
  // If physical page is not allocated (page fault has not been occurred on that address), 
  // just remove mmap_area structure.
  mmap->isUsed = 0; 
  mmap->f = 0;
  mmap->addr = 0; 
  mmap->length = 0; 
  mmap->offset = 0; 
  mmap->prot = 0; 
  mmap->flags = 0; 
  mmap->p = 0; 

  return 1; 
}
// Notice) In one mmap_area, situation of some of pages are allocated and some
// are not can happen.


// syscall to return current number of free memory pages 
// refer. kalloc.c !!
int freemem(void){
  return freememCount(); 
}  
