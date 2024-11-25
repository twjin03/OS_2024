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
int num_free_pages;
int num_lru_pages;

struct{
  struct spinlock lock; 
  char *bitmap; 
} swap;  // !!! 일단 일케... ???

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
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
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
char*
kalloc(void)
{
  struct run *r;

//try_again:
  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
//  if(!r && reclaim())
//	  goto try_again;
  if(r)
    kmem.freelist = r->next;
  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
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
  num_lru_pages++;
}

// lru_remove
void lru_remove(struct page *page){
  if (page->next == page){ // single node
    page_lru_head = 0; 
  }
  else{
    page->prev->next = page->next;
    page->next->prev = page->prev;
    if (page_lru_head == page)
    page_lru_head = page->next; 
  }
  page->next = page->prev = 0; 
  num_lru_pages--; 
}

// select_victim 
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
  blkno = find_blkno(); 
  if (blkno<0){
    release(&swap.lock); 
    panic("swapout: No swap space"); 
  } 
  set_bitmap(blkno); 
  release(&swap.lock); 

  // write data in swap space
  swapwrite(P2V(victim->vaddr), blkno); 

  // update page status 
  victim->vaddr = (char *)(blkno << SWAP_OFFSET);  // ??? ???
  victim->pgdir = 0; 
  victim->swapped = 1; // ???

  pte_t *pte = walkpgdir(victim->pgdir, victim->vaddr, 0); 
  if (pte) {
    *pte = (blkno << SWAP_OFFSET) | PTE_SWAP; // Mark the PTE as swapped with the swap slot address
  }

  lru_remove(P2V(victim->vaddr)); 

  kfree(P2V(victim->vaddr)); 
  
}
  


// 3. swap-in
// swapin
struct page* swapin(pde_t *pgdir, char *vaddr) {
  // victim page) backing store -> main mem.

  char *new_page = kalloc();
  if (!new_page) return 0; // OOM 처리

  pte_t *pte = walkpgdir(pgdir, vaddr, 0);
  if (!pte || !(*pte & PTE_SWAP)) {
    panic("swapin: Invalid swap PTE");
  }

  int blkno = (*pte) >> SWAP_OFFSET; // ???
  swapread(new_page, blkno);

  *pte = V2P(new_page) | PTE_P | PTE_W | PTE_U;

  acquire(&swap.lock);
  clear_swap_slot(blkno);
  release(&swap.lock);

  struct page *page = find_page(pgdir, vaddr); // ???
  page->vaddr = vaddr;
  page->pgdir = pgdir;
  page->swapped = 0;

  lru_add(page);
  return page;
}


// 4. Bitmap mgmt
int find_blkno(){ // ???
  for (int i = 0; i < SWAPMAX; i++){
    if(!swap.bitmap[i]) return i; 
  }
  return -1; // fail
}

void set_bitmap(int blk){
  swap.bitmap[blk] = 1; 
}

void clear_bitmap(int blk) {
  swap.bitmap[blk] = 0;
}