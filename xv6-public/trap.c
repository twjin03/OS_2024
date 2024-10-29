#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"



// 시스템 interrupt 및 예외 처리 
// syscall, timer interrrupt와 같은 주요 trap 


// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

// Interrupt Descriptor Table (IDT)를 초기화 
void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)  //256개의 interrupt 벡터에 SETGATE 매크로 사용해 게이트 설정 
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);
  // T_SYSCALL 트랩은 사용자 모드에서 사용할 수 있도록 설정 
  initlock(&tickslock, "time");  // tickslock이라는 스핀락을 초기화 -> ticks 변수를 보호하는 데 사용 
}


// IDT를 memory에 load 
void
idtinit(void)
{
  lidt(idt, sizeof(idt));  // IDT를 로드하여 인터럽트가 발생할 수 있도록 준비 
}


//interrupt와 예외를 처리하는 메인 핸들러 
//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  
  //!!!!!!!!테스트를 위한 임시 코드 !!!!!!!
  if((ticks%20==0)&&(ticks>=200)&&myproc()) {
    cprintf("\n\n");
    ps(0);
    cprintf("\n\n");
  }




  if(tf->trapno == T_SYSCALL){ // T_SYSCALL이 발생하면 
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;  // process의 trapframe을 저장하고
    syscall();          // syscall 함수를 호출하여 시스템 콜을 처리 
    if(myproc()->killed)
      exit();
    return;
  }

  //(pa2) 조건문 추가 
  if(myproc() && myproc()->state == RUNNING && tf->trapno == T_IRQ0+IRQ_TIMER){
    
    struct proc *p = myproc();
    int delta_runtime = 1000; // 각 tick이 1000ms


    //runtime업데이트
    p->runtime += delta_runtime;

    //vruntime업데이트
    int weight = weight_table[p->nice];
    p->vruntime += ((delta_runtime * weight_table[20]) / weight);

    //runtime_d_weight업데이트 
    p->runtime_d_weight = p->runtime / weight_table[p->nice];

    p->time_slice--;
    if(p->time_slice<=0){
      p->time_slice = 0;
      yield();
    }

}


  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}

// trap 함수는 프로세스가 종료되었는지(killed 상태) 확인하고, 종료가 필요할 경우 프로세스를 강제로 종료함
// 타이머 인터럽트가 발생하면 yield를 호출해 프로세스가 CPU를 양보하도록 함
