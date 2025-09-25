// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

// 加入一个 superpage 映射队列，但是初始化时不进行物理分配
struct runSuperpg {
  struct runSuperpg *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  struct runSuperpg *superpagefreelist;
} kmem;

void kinit() {
  initlock(&kmem.lock, "kmem");
  freerange(end, (void *)PHYSTOP);
}

// 加入多个 superpage
void
freerange(void *pa_start, void *pa_end)
{
  // char *p;
  // p = (char*)SUPERPGROUNDUP((uint64)pa_start);
  // // 创建 10 个 superpage
  // int need = 10;
  // for(; p + SUPERPGSIZE < (char*)pa_end; p += SUPERPGSIZE){
  //   if(need >0 && ( (uint64)p % SUPERPGSIZE) == 0){
  //     struct runSuperpg *spg = (struct runSuperpg*)p;
  //     spg->next = kmem.superpagefreelist;
  //     kmem.superpagefreelist = spg;
  //   }
  //   else{
  //     kfree(p);
  //     p+= PGSIZE;
  //   }
  //   need--;
  // }
  // p = (char*)PGROUNDUP((uint64)p);
  // for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
  //   kfree(p);
  char *p;
  int super_need = 20; 

  // 先把起始地址对齐到 2MB
  p = (char*)SUPERPGROUNDUP((uint64)pa_start);

  // 优先放 superpage
  while (super_need > 0 && p + SUPERPGSIZE <= (char*)pa_end) {
    if (((uint64)p % SUPERPGSIZE) == 0) {
      struct runSuperpg *spg = (struct runSuperpg*)p;
      spg->next = kmem.superpagefreelist;
      kmem.superpagefreelist = spg;
      p += SUPERPGSIZE;
      super_need--;
    } else {
      break; // 理论上不会发生，因为已经对齐了
    }
  }

  // 剩余部分全部按 4KB 粒度放入普通 freelist
  p = (char*)PGROUNDUP((uint64)p);
  for (; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa) {
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(void) {
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  return (void *)r;
}

void *superalloc(void) {
  // 假定 一个 超页是由512 个连续的 4096 byte 物理页组成
  // 剔除上述假设，此时是初始化 10 个 superpage
  struct runSuperpg *spg;

  acquire(&kmem.lock);
  spg = kmem.superpagefreelist;
  if (spg)
    kmem.superpagefreelist = spg->next;
  kmem.superpagefreelist = spg->next;
  release(&kmem.lock);

  if (spg) {
    memset((char *)spg, 5, SUPERPGSIZE); // 用 junk 填充
  }
  return (void *)spg;
}

void superfree(void *pa) {
  struct runSuperpg *spg;

  if (((uint64)pa % SUPERPGSIZE) != 0 || (char *)pa < end ||
      (uint64)pa >= PHYSTOP)
    panic("superfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, SUPERPGSIZE);

  spg = (struct runSuperpg *)pa;

  acquire(&kmem.lock);
  spg->next = kmem.superpagefreelist;
  kmem.superpagefreelist = spg;
  release(&kmem.lock);
}