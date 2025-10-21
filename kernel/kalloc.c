// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define KPAstart (PGROUNDUP((uint64)end))
#define KPAend ((uint64)PHYSTOP)
#define MaxPG  (PHYSTOP - KERNBASE) / PGSIZE
#define NPG  ((uint64)(KPAend - KPAstart) / (uint64)PGSIZE);
static uint64 free_memory = 0;

uint64 refer_count[MaxPG];
struct spinlock ref_lock;

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&ref_lock, "ref_lock");
  free_memory = NPG;
  memset(refer_count, 0, sizeof(refer_count));
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  // struct run *r;

  // if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
  //   panic("kfree");

  // // Fill with junk to catch dangling refs.

  // uint64 PGindex = ((uint64)pa - KPAstart) / PGSIZE ;
  // uint64 count = refer_count[PGindex];
  // if(count == 0){
  //   memset(pa, 1, PGSIZE);
  //   r = (struct run*)pa;

  //   acquire(&kmem.lock);
  //   r->next = kmem.freelist;
  //   free_memory++;
  //   kmem.freelist = r;

  //   release(&kmem.lock);
  // }
  // else if(count == 1){
  //   refer_count[PGindex] = 0;
  //   memset(pa, 1, PGSIZE);
  //   r = (struct run*)pa;

  //   acquire(&kmem.lock);
  //   r->next = kmem.freelist;
  //   kmem.freelist = r;
  //   free_memory++;
  //   //printf("%ld",free_memory);
  //   release(&kmem.lock);
  // }
  // else if(count > 1){
  //   acquire(&ref_lock);
  //   refer_count[PGindex]--;
  //   release(&ref_lock);
  // }
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.

  uint64 PGindex = ((uint64)pa - KPAstart) / PGSIZE ;
  //uint64 count = refer_count[PGindex];

  // if(count <= 1){
  //   memset(pa, 1, PGSIZE);

  //   r = (struct run*)pa;

  

  //   acquire(&kmem.lock);
  //   r->next = kmem.freelist;
  //   kmem.freelist = r;
  //   release(&kmem.lock);
  // }
  // else if( count > 1){
  //   refer_count[PGindex]--;
  // }
  // 递减引用计数
  acquire(&ref_lock);

  if(refer_count[PGindex] > 0){
    refer_count[PGindex]--;
    if(refer_count[PGindex] > 0){
      release(&ref_lock);
      return;
    }
  }
  release(&ref_lock);

  // 将页放回freelist中
  memset(pa, 1, PGSIZE);
  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  free_memory++;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  uint64 rPA,index;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
    free_memory--;
    //printf("%ld",free_memory);
    rPA = (uint64)r;
    index = (rPA - KPAstart) / PGSIZE;
    acquire(&ref_lock);
    refer_count[index]++;
    release(&ref_lock);
  }
  release(&kmem.lock);
  
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
