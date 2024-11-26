// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run { // page linked-list
  struct run *next;
};

struct { // physical memory mgmt 
  struct spinlock lock;
  int use_lock;
  struct run *freelist;  
} kmem;

struct page pages[PHYSTOP/PGSIZE];
struct page *page_lru_head;
int num_free_pages = PHYSTOP/PGSIZE;
int num_lru_pages;

struct{
  struct spinlock lock; 
  char *bitmap; 
} swap;  // !!! 일단 일케... ???

struct spinlock lru_lock; 

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;

  // // pa4) 
  // // initialize pages[]
  // for (int i = 0; i < PHYSTOP/PGSIZE; i++){
  //   pages[i].pgdir = 0; 
  //   pages[i].vaddr = 0; 
  //   pages[i].next = 0; 
  //   pages[i].prev = 0; 
  // }


  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;

  // initialize swap space bitmap
  initlock(&swap.lock, "swap_bitmap");  
  swap.bitmap = kalloc(); // • Use 1 physical page for bitmap to track swap space
  if (!swap.bitmap)
    panic("init_swap_bitmap: Failed to allocate bitmap page");
  memset(swap.bitmap, 0, PGSIZE); 

  // initialize LRU list 
  initlock(&lru_lock, "lru_lock"); 
  num_lru_pages = 0; 

}
// initialize pages in given memory area and insert to linked-list by calling kfree
void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

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


char* kalloc(void) {
  struct run *r;

try_again:
  if (kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;

  if (!r) {
    if (kmem.use_lock)
      release(&kmem.lock);

    // attempt page reclaim
    if (!reclaim()) {
      // If reclaim fails
      cprintf("kalloc: OOM after reclaim attempt\n");
      return 0; // Allocation failed
    }
    // After reclaiming, retry allocation
    goto try_again;
  }

  // Allocate the page from freelist
  kmem.freelist = r->next;

  if (kmem.use_lock)
    release(&kmem.lock);

  return (char*)r;
}


int reclaim(){
  struct page *victim = select_victim(); 

  if (!victim){
    cprintf("reclaim: OOM\n"); 
    return 0; // fail 
  }

  swapout(victim); 
  return 1; // success
}


// • Implement page-level swapping
// – Swap-in: move the victim page from backing store to main memory
// – Swap-out: move the victim page from main memory to backing store

// • Manage swappable pages with LRU list
// – Page replacement policy: clock algorithm

// • Codes you need to create or modify in xv6
// – Swap-in, swap-out operation
// – LRU list management
// – Some extras



// ** Swappable Pages in xv6 

// • Only user pages are swappable
  // – Some of physical pages should not be swapped out
  // • E.g., page table pages
  // – So, manage swappable pages with LRU list (circular doubly linked list)
  // • When init/alloc/dealloc/copy user virtual memories

// • Page replacement algorithm: clock algorithm
  // – Use A (Accessed) bit in each PTE (PTE_A : 0x20)
  // – From lru_head, select a victim page following next pointer
  // • If PTE_A==1, clear it and send the page to the tail of LRU list
  // • If PTE_A==0, evict the page (victim page)
  // – QEMU automatically sets PTE_A bit when accessed
// • If free page is not obtained through the kalloc() function,
// swap-out the victim page


// ** Swap-out operation in xv6
// 1. Use swapwrite() function, write the victim page in swap space
// – swapwrite() will be provided in skeleton code
// 2. Victim page’s PTE will be set as swap space offset
// 3. PTE_P will be cleared


// ** Swap-in Opertion in xv6
// • When accessing a page that has been swapped out
// 1. Get new physical page
// 2. Using swapread() function, read from swap space to
// physical page
// • swapread() will be provided in skeleton code
// 3. Change PTE value with physical address & set PTE_P
// • Tip: do not need to call mappages(), because page table had alre
// ady been allocated



// ** Several Considerations and Assumptions
// • Use 1 physical page for bitmap to track swap space
// – Bit in bitmap is set when page swapped out to swap space
// – Bit in bitmap is cleared when page swapped in

// • When user virtual memory is copied
// – Present pages should be copied
// – Swapped-out pages should also be copied

// • When user virtual memory is deallocated
// – Present pages should be freed, set PTE bits to 0 and remove
// them from LRU list
// – Swapped-out pages should be cleared in bitmap and set PTE
// bits to 0

// • When swap-out should be occurred and there is no page in LRU list,
// OOM(Out of memory) error should occur
// • Inside the kalloc function, just cprintf error message
// • kalloc should return 0 when OOM occurs
// • Lock should be considered with shared resource for synchronization
// • All pages are managed in a struct page
// – Already implemented in skeleton code (mmu.h)



// pa4) start!

// 1. LRU list mgmt) 
// clock algorithm 
// only user pages are swappable! 
// -> manage swappable pages with LRU list
  // not in use 
  // swappable page
  // unswappable page
// (circular doubly linked list)
// • When init/alloc/dealloc/copy user virtual memories


// lru_add
  // add page to the tail of LRU list 
  // (circular doubly linked list)
void lru_add(struct page *page){ // 추가할 page를 인자로 받음
  acquire(&lru_lock);

  if(!page_lru_head){
    page_lru_head = page; 
    page->next = page; 
    page->prev = page; 
  }
  else{ // tail에 추가
    struct page *tail = page_lru_head->prev; 
    tail->next = page; 
    page->prev = tail; 
    page->next = page_lru_head; 
    page_lru_head->prev = page; 
  }
  num_lru_pages++; // swappable page ???
  num_free_pages--; // not in use ???

  release(&lru_lock);
}

// lru_remove
void lru_remove(struct page *page){
acquire(&lru_lock);

  if (page->next == page){ // single node
    page_lru_head = 0; 
  }
  else{
    page->prev->next = page->next;
    page->next->prev = page->prev;
    if (page_lru_head == page)
    page_lru_head = page->next; 
  }
  page->next = page->prev = 0;  // memset 이용 ???
  num_lru_pages--; 
  num_free_pages++; 

  release(&lru_lock);
}

// select_victim ) LRU 
struct page* select_victim(){
  struct page *current = page_lru_head; 

  while (num_lru_pages > 0){
    pte_t *pte = walkpgdir(current->pgdir, current->vaddr, 0); 
    if (!pte) panic("select_victim: invalid PTE"); // ???

    if (*pte & PTE_A){ // If PTE_A==1,
      *pte &= ~PTE_A; // clear it
      current = current->next;  // and send the page to the tail of LRU list
    }
    else{ // If PTE_A==0
      return current; // evict the page (victim page)
    }
  }
  return 0; // fail to select victim -> OOM  ???
}



// 2. swap-out
// swapout
void swapout(struct page *victim){
  // victim page) main mem. -> backing store

  // allocate swap space
  int blkno; 

  acquire(&swap.lock);
  blkno = find_free_blkno(); 
  if (blkno < 0){
    release(&swap.lock); 
    panic("swapout: No swap space"); 
  } 
  set_bitmap(blkno); 
  release(&swap.lock); 

  // write data in swap space (swap data out)
  swapwrite(P2V(victim->vaddr), blkno); 

  // Update PTE and victim status
  pte_t *pte = walkpgdir(victim->pgdir, victim->vaddr, 0);
  if (!pte) {
    panic("swapout: Invalid PTE");
  }
  *pte = (blkno << SWAP_OFFSET) | PTE_SWAP; // Set PTE as swapped
  victim->swapped = 1;

  lru_remove(P2V(victim->vaddr)); 
}
  


// 3. swap-in
// swapin
struct page* swapin(pde_t *pgdir, char *vaddr) {
  // victim page) backing store -> main mem.

  char *new_page = kalloc(); // 1. Get new physical page
  if (!new_page) return 0; // OOM 처리

  pte_t *pte = walkpgdir(pgdir, vaddr, 0);
  if (!pte || !(*pte & PTE_SWAP)) {
    panic("swapin: Invalid swap PTE");
  }

  int blkno = (*pte) >> SWAP_OFFSET; // ???
  swapread(new_page, blkno); // 2. Using swapread() function, read from swap space to physical page


  *pte = V2P(new_page) | PTE_P | PTE_W | PTE_U;
  // 3. Change PTE value with physical address & set PTE_P

  acquire(&swap.lock);
  clear_bitmap(blkno);
  release(&swap.lock);

  struct page *page = &pages[V2P(new_page) / PGSIZE];
  page->vaddr = vaddr;
  page->pgdir = pgdir;
  page->swapped = 0;

  lru_add(page);
  return page;
}


// 4. Bitmap) swap space mgmt
// Use a bitmap to track swap space (1 page of physical memory allocated for this purpose).
// Each bit in the bitmap corresponds to one page in the swap space.
void set_bitmap(int blkno) {
  int index = blkno - SWAPBASE; // SWAPBASE 기준으로 블록 번호 변환
  if (index < 0 || index >= SWAPMAX) {
    panic("set_bitmap: Invalid blkno");
  }

  acquire(&swap.lock); 
  swap.bitmap[index / 8] |= (1 << (index % 8)); // 해당 비트를 1로 설정
  release(&swap.lock); 
}

void clear_bitmap(int blkno) {
  int index = blkno - SWAPBASE; // SWAPBASE 기준으로 블록 번호 변환
  if (index < 0 || index >= SWAPMAX) {
    panic("clear_bitmap: Invalid blkno");
  }

  acquire(&swap.lock); 
  swap.bitmap[index / 8] &= ~(1 << (index % 8)); // 해당 비트를 0으로 설정
  release(&swap.lock); 
}

// ???
int is_blk_used(int blkno) {
  int index = blkno - SWAPBASE; // SWAPBASE 기준으로 블록 번호 변환
  if (index < 0 || index >= SWAPMAX) {
    panic("is_blk_used: Invalid blkno");
  }

  acquire(&swap.lock);
  int used = (swap.bitmap[index / 8] & (1 << (index % 8))) != 0; // 비트 확인
  release(&swap.lock); 
  return used;
}

// ??? ???
// 빈 스왑 블록 탐색
int find_free_blkno() {
  acquire(&swap.lock); // 락 획득

  for (int i = 0; i < SWAPMAX; i++) {
    if (!(swap.bitmap[i / 8] & (1 << (i % 8)))) { // 빈 비트 확인
      swap.bitmap[i / 8] |= (1 << (i % 8)); // 비트를 1로 설정 (할당)
      release(&swap.lock); // 락 해제
      return SWAPBASE + i; // 실제 블록 번호 반환
    }
  }

  release(&swap.lock); // 락 해제
  return -1; // 빈 블록 없음
}

// 스왑 블록 해제
void free_blkno(int blkno) {
  clear_bitmap(blkno); // 비트 해제
}
