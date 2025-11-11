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

struct kmem{
  struct spinlock lock;
  struct run *freelist;
} ;

// 不修改 kmem 的命名，为 每个 CPU 设置一个 freelist 以及对应的锁
struct kmem FreeList[NCPU];

void
kinit()
{
  //initlock(&kmem.lock, "kmem");
  // initialize lock for each CPU freelist
  for(int i = 0; i < NCPU; i++){
    char name[16];
    snprintf(name, sizeof(name), "kmem_CPU%d", i);
    initlock(&FreeList[i].lock, name);
  }

  freerange(end, (void*)PHYSTOP);
}
// void
// freerange(void *pa_start, void *pa_end)
// {
//   char *p;
//   p = (char*)PGROUNDUP((uint64)pa_start);
//   for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
//     kfree(p);
// }

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  int PG = ((uint64)PHYSTOP - PGROUNDUP((uint64)end)) / PGSIZE;
  int CPUPG = PG / NCPU;
  int overflow = PG % NCPU;

  int EachCPUPG[NCPU];
  for(int j = 0; j < NCPU; j++){
    EachCPUPG[j] = (j < overflow ? CPUPG + 1 : CPUPG);
  }

  // for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
  //   if(p == (char*)PGROUNDUP((uint64)pa_start) + (sum(i) - 1) * PGSIZE && i < NCPU -1){
  //     i++;
  //   }
  //   kfree_CPU(i, p);
  // }
  for(int z = 0; z < NCPU; z++){
    int q = 0;
    while(q < EachCPUPG[z] && p <= (char*)pa_end){
      kfree_CPU(z, p);
      p += PGSIZE;
      q++;
    }
  }
}

int
sum(int cpu){
  int PG = ((uint64)PHYSTOP - PGROUNDUP((uint64)end)) / PGSIZE;
  int EachCPUPG = PG / NCPU;
  return cpu * EachCPUPG; 
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
  // memset(pa, 1, PGSIZE);

  // r = (struct run*)pa;

  // acquire(&kmem.lock);
  // r->next = kmem.freelist;
  // kmem.freelist = r;
  // release(&kmem.lock);


  struct run *r;

  // 关闭中断
  push_off();
  
  // 判断当前处于哪一个 CPU
  int cpu = cpuid();

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&FreeList[cpu].lock);
  r->next = FreeList[cpu].freelist;
  FreeList[cpu].freelist = r;
  release(&FreeList[cpu].lock);

  pop_off();
}

// actually only used in init stage , maybe not need to push_off and pop_off
void
kfree_CPU(int cpu , void *pa)
{
  // 关闭中断
  // push_off();

  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree_CPU");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&FreeList[cpu].lock);
  r->next = FreeList[cpu].freelist;
  FreeList[cpu].freelist = r;
  release(&FreeList[cpu].lock);

  // pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  // struct run *r;

  // acquire(&kmem.lock);
  // r = kmem.freelist;
  // if(r)
  //   kmem.freelist = r->next;
  // release(&kmem.lock);

  // if(r)
  //   memset((char*)r, 5, PGSIZE); // fill with junk
  // return (void*)r;

  struct run *r;

  // 关闭中断
  push_off();

  // 获取 cpu number
  int cpu = cpuid();

  acquire(&FreeList[cpu].lock);
  r = FreeList[cpu].freelist;
  if(r != 0){
    struct run *next = FreeList[cpu].freelist->next;
    if(next != 0)
      FreeList[cpu].freelist = next;
    else{
      FreeList[cpu].freelist = 0;
    }
    release(&FreeList[cpu].lock);
  }
  else{
    release(&FreeList[cpu].lock);
    r = (struct run*)fetch_from_other_cpu(cpu);
  }

  if(r)
    memset((char*)r, 5, PGSIZE);

  // 启用中断
  pop_off();

  return (void*)r;
}


// 从另一个 CPU 的 freelist 中直接继承所有空余的内存 --> don't know to inherit one or the rest
// maybe inherit the rest would cause deadlock ?
void *
fetch_from_other_cpu(int cpu)
{
  struct run *r = 0;
  for(int i = 0; i < NCPU; i++){
    if(cpu == i) continue;
    acquire(&FreeList[i].lock);
    if(FreeList[i].freelist){
      r = FreeList[i].freelist;
      if(FreeList[i].freelist->next)
        FreeList[i].freelist = FreeList[i].freelist->next;
      else{
        FreeList[i].freelist = 0;
      }
      acquire(&FreeList[cpu].lock);
      FreeList[cpu].freelist = r;
      release(&FreeList[cpu].lock);
      release(&FreeList[i].lock);
      break;
    }
    release(&FreeList[i].lock);
  }
  return (void*)r;
}
