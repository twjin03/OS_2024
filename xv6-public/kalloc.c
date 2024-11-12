// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

// 사용자 프로세스, 커널 스택, 페이지 테이블 페이지, 파이프 버퍼를 위한 물리 메모리를 
// 4096바이트 크기의 페이지 단위로 할당하는 physical memory allocator 


#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file 커널이 ELF 파일에서 로드된 후의 첫 번째 주소
                   // defined by the kernel linker script in kernel.ld

struct run { //freelist에 있는 각 페이지를 가리킴. next는 다음 free page를 가리키는 포인터 
  struct run *next;
};

struct { // 메모리 할당을 관리하는 구조체 
  struct spinlock lock; // 동기화를 위한 스핀락 
  int use_lock; // 잠금을 사용할지 여부를 나타냄 
  struct run *freelist;  // free memory page들의 linked-list의 시작점을 가리킴 
} kmem;


//pa3) 모든 physical memory가 부트 타입의 kmem의 freelist에 포함됨

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.

void //pa3) kinit1() sets up for lock-less allocation in the first 4MB
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem"); // 초기화 
  kmem.use_lock = 0; // use_lock 0으로 설정하여 락 없이 메모리를 할당할 수 있게 함 
  freerange(vstart, vend); // freerange함수를 호출하여 메모리를 freelist에 추가. 초기 4MB 메모리 범위에 대한 설정 담당함
}

void //pa3) kinit2() arranges for more memory (until PHYSTOP) to be allocatable (224MB)
kinit2(void *vstart, void *vend) // kinit1 다음에 호출되어 나머지 physical memory를 freelist에 추가. 
{
  freerange(vstart, vend); 
  kmem.use_lock = 1; // 메모리 할당 시 락을 사용 
}

//pa3) freerange() kfree() with page size unit

void
freerange(void *vstart, void *vend) // 지정된 메모리 범위를 page 단위로 freelist에 추가 
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart); // 시작 주소를 페이지 크기에 맞춰 올림 처리 
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p); // 페이지 단위로 순회하며 kfree 호출하여 메모리 해제 
} 
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)



// pa3) freemem 구현 위한 fmemCount 계산 
uint fmemCount; 

void //pa3) kfree() fills page with 1s, and put it into freelist (page pool)
kfree(char *v) // physical memory의 페이지를 해제 
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP) // 해제하려는 페이지의 유효성을 검사하고 (PGSIZE로 나눠 떨어지는지, end보다 큰지, PHYSTOP보다 작은지 확인), 유효하지 않으면 panic 호출로 예외를 발생시킴
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);
  // fmemCount 증가시킴 
  fmemCount++; 

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;
  fmemCount--; 

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}

// 값 반환 위한 함수 
uint freememCount(void){
  return fmemCount; 
}

