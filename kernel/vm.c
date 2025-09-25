#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[]; // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t kvmmake(void) {
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t)kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

#ifdef LAB_NET
  // PCI-E ECAM (configuration space), for pci.c
  kvmmap(kpgtbl, 0x30000000L, 0x30000000L, 0x10000000, PTE_R | PTE_W);

  // pci.c maps the e1000's registers here.
  kvmmap(kpgtbl, 0x40000000L, 0x40000000L, 0x20000, PTE_R | PTE_W);
#endif

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext,
         PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);

  return kpgtbl;
}

// Initialize the kernel_pagetable, shared by all CPUs.
void kvminit(void) { kernel_pagetable = kvmmake(); }

// Switch the current CPU's h/w page table register to
// the kernel's page table, and enable paging.
void kvminithart() {
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc) {
  if (va >= MAXVA)
    panic("walk");

  for (int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if (*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
#ifdef LAB_PGTBL
      if (PTE_LEAF(*pte)) {
        return pte;
      }
#endif
    } else {
      if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

pte_t *walk_level(pagetable_t pagetable, uint64 va, int level, int alloc) {
  if (va >= MAXVA)
    panic("walk");

  for (int size = 2; size > level; size--) {
    pte_t *pte = &pagetable[PX(size, va)];
    if (*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
#ifdef LAB_PGTBL
      // 此处已经支持当为叶子页时就返回 pte
      if (PTE_LEAF(*pte)) {
        return pte;
      }
#endif
    } else {
      // 需要加上一个超页逻辑
      if (level == 0) {
        if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
          return 0;
        memset(pagetable, 0, PGSIZE);
        *pte = PA2PTE(pagetable) | PTE_V;
      } else if (level == 1) {
        if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
          return 0;
        memset(pagetable, 0, PGSIZE);
        *pte = PA2PTE(pagetable) | PTE_V;
      }
    }
  }
  return &pagetable[PX(level, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64 walkaddr(pagetable_t pagetable, uint64 va) {
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    return 0;
  if ((*pte & PTE_V) == 0)
    return 0;
  if ((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

#if defined(LAB_PGTBL) || defined(SOL_MMAP) || defined(SOL_COW)
void vmprint(pagetable_t pagetable) {
  // your code here
  // set the depth of the tree
  // first print the address of pagetable
  static int level = 2;
  static uint64 path[3] = {0, 0, 0};
  if (level == 2)
    printf("page table %p\n", pagetable);
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    path[level] = i;
    if (level == 2) {
      // reset the segment
      path[1] = 0;
      path[0] = 0;
    }
    if (level == 1) {
      path[0] = 0;
    }
    if ((pte & PTE_V)) {
      // print current information
      for (int index = 3 - level; index > 0; index--)
        printf(" ..");
      uint64 va = ((path[2]) << 30) | ((path[1]) << 21) | ((path[0]) << 12);
      printf("%p: pte %p pa %p\n", (void *)va, (void *)pte,
             (void *)PTE2PA(pte));
    }
    if ((pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      uint64 child = PTE2PA(pte);
      if (child == 0)
        continue;
      // print its child information
      pagetable_t child_pg = (pagetable_t)child;
      // print its child information
      level--;
      vmprint(child_pg);
      level++;
    }
  }
}
#endif

// 将一个 对齐的 superpage 拆成 512 个 普通 page
int degradeSuperpage(pagetable_t pagetable, uint64 super_va) {
  // 首先确认 super_va 是 2 MB 对齐的
  pte_t *le1pte = walk_level(pagetable, super_va, 1, 0);
  // 确认该 level 1 的 PTE 是叶子页
  if (le1pte == 0 || !PTE_LEAF(*le1pte))
    return -1;

  // 得到物理地址和其 FLAG
  uint64 pa_start = PTE2PA(*le1pte);
  uint64 pa_FLAG = PTE_FLAGS(*le1pte) & ~PTE_V;

  // flag remove PTE_V

  // 刷新 TLB
  *le1pte = 0;
  sfence_vma();

  // 分配 普通 page 页表
  pte_t *le0pte = (pte_t *)kalloc();
  // 分配普通页表失败
  if (le0pte == 0) {
    // 存储 Level 1 的映射回去
    *le1pte = PA2PTE(pa_start) | pa_FLAG;
    return -1;
  }
  memset(le0pte, 0, PGSIZE);

  // 在 level 1 入口处设置 到 level 0 页表的入口
  *le1pte = PA2PTE((uint64)le0pte) | PTE_V;

  // 将 superpage 映射为 512 个普通 page。同时把原先的 FLAG 安装到新的 page上
  for (int i = 0; i < 512; i++) {
    le0pte[i] = PA2PTE(pa_start + (uint64)i * PGSIZE) | pa_FLAG | PTE_V;
  }

  sfence_vma();
  return 0;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm) {
  if (mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa,
             int perm) {
  uint64 a, last;
  pte_t *pte;

  if ((va % PGSIZE) != 0)
    panic("mappages: va not aligned");

  if ((size % PGSIZE) != 0)
    panic("mappages: size not aligned");

  if (size == 0)
    panic("mappages: size");

  a = va;
  last = va + size - PGSIZE;
  for (;;) {
    if ((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if (*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// antimate the mappages function and make a new function to map super page
int mapSuperpages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa,
                  int perm) {
  uint64 a, last;
  pte_t *pte;

  if ((va % SUPERPGSIZE) != 0)
    panic("mapSuperpages: va not aligned");

  if ((size % SUPERPGSIZE) != 0)
    panic("mapSuperpages: size not aligned");

  if (size == 0)
    panic("mapSuperpages: size");

  a = va;
  last = va + size - SUPERPGSIZE;
  for (;;) {
    // 关键在于这步，如何在 level 1 建立映射
    if ((pte = walk_level(pagetable, a, 1, 1)) == 0)
      return -1;
    if (*pte & PTE_V)
      panic("mapSuperpages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last)
      break;
    a += SUPERPGSIZE;
    pa += SUPERPGSIZE;
  }
  return 0;
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t uvmcreate() {
  pagetable_t pagetable;
  pagetable = (pagetable_t)kalloc();
  if (pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// // Remove npages of mappings starting from va. va must be
// // page-aligned. It's OK if the mappings don't exist.
// // Optionally free the physical memory.
// void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free) {
//   uint64 a;
//   pte_t *pte;
//   int sz = PGSIZE;

//   if ((va % PGSIZE) != 0)
//     panic("uvmunmap: not aligned");

//   for (a = va; a < va + npages * PGSIZE; a += sz) {
//     if ((pte = walk(pagetable, a, 0)) == 0) // leaf page table entry
//     allocated?
//       continue;
//     if ((*pte & PTE_V) == 0) // has physical page been allocated?
//       continue;
//     sz = PGSIZE;
//     if (PTE_FLAGS(*pte) == PTE_V)
//       panic("uvmunmap: not a leaf");
//     if (do_free) {
//       uint64 pa = PTE2PA(*pte);
//       kfree((void *)pa);
//     }
//     *pte = 0;
//   }
// }

// Remove npages of mappings starting from va. va must be
// page-aligned. It's OK if the mappings don't exist.
// Optionally free the physical memory.
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free) {
  // 这部分如何修改支持
  uint64 a;
  pte_t *pte;
  int sz = PGSIZE;

  if ((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for (a = va; a < va + npages * PGSIZE; a += sz) {
    pte = walk_level(pagetable, a, 1, 0);
    // 判断是否 level 1 为 叶子页 / superpage
    // judge whether the level 1 entry is a leaf page
    if (pte && PTE_LEAF(*pte)) {
      uint64 superStart_va = SUPERPGROUNDDOWN(a);
      uint64 superEnd_va = SUPERPGROUNDUP(a);
      if (a == superStart_va &&
          (a + npages * PGSIZE - (a - va)) >= superEnd_va) {
        // 将整个 superpage 映射取消
        // unmap the whole superpage
        if (do_free)
          superfree((void *)PTE2PA(*pte));

        *pte = 0;
        a = superEnd_va;
        sfence_vma();
        continue;
      } else {
        // 不对齐的情况
        // The situation that is not aligned
        if (degradeSuperpage(pagetable, superStart_va) < 0) {
          panic("degradeSuperpage failed");
        }
        continue;
      }
    }

    // 即level 1 不是叶子页的情况
    // namely the situation that level 1 is not a leaf
    if ((pte = walk(pagetable, a, 0)) == 0) // leaf page table entry allocated?
      continue;
    if ((*pte & PTE_V) == 0) // has physical page been allocated?
      continue;
    sz = PGSIZE;
    if (PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if (do_free) {
      uint64 pa = PTE2PA(*pte);
      // 这里就要判断该怎么释放了
      kfree((void *)pa);
    }
    *pte = 0;
  }
}

// void
// uvmunmapSuper(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
// {
//   uint64 a;
//   pte_t *pte;
//   int sz = SUPERPGSIZE;

//   if((va % SUPERPGSIZE) != 0)
//     panic("uvmunmapsuper: not aligned");

//   for(a = va; a < va + npages*SUPERPGSIZE; a += sz){
//     if((pte = walk(pagetable, a, 0)) == 0) // leaf page table entry
//     allocated?
//       continue;
//     if((*pte & PTE_V) == 0)  // has physical page been allocated?
//       continue;
//     sz = SUPERPGSIZE;
//     if(PTE_FLAGS(*pte) == PTE_V)
//       panic("uvmunmapsuper: not a leaf");
//     if(do_free){
//       uint64 pa = PTE2PA(*pte);
//       superfree((void*)pa);
//     }
//     *pte = 0;
//   }
// }

// // Allocate PTEs and physical memory to grow process from oldsz to
// // newsz, which need not be page aligned.  Returns new size or 0 on error.
// uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
// {
//   char *mem;
//   uint64 a;
//   int sz;

//   if (newsz < oldsz)
//     return oldsz;

//   oldsz = PGROUNDUP(oldsz);
//   for (a = oldsz; a < newsz; a += sz) {
//     sz = PGSIZE;
//     mem = kalloc();
//     if (mem == 0) {
//       uvmdealloc(pagetable, a, oldsz);
//       return 0;
//     }
// #ifndef LAB_SYSCALL
//     memset(mem, 0, sz);
// #endif
//     if (mappages(pagetable, a, sz, (uint64)mem, PTE_R | PTE_U | xperm) != 0)
//     {
//       kfree(mem);
//       uvmdealloc(pagetable, a, oldsz);
//       return 0;
//     }
//   }
//   return newsz;
// }

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm) {
  char *mem;
  uint64 a;
  // int sz;

  // 如果新内存小于原内存 就什么也不干
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);

  // 如果二者内存差 大于 2 MB 则分配 2 MB 内存 好像只需要分配 2 MB
  uint64 superStart = SUPERPGROUNDUP(a);
  // 分配 4 KB 直到下个 superpage boundary
  for(; a < newsz && a < superStart; a += PGSIZE){
    mem = kalloc();
    if(mem == 0) goto err1;
#ifndef LAB_SYSCALL
    memset(mem, 0, PGSIZE);
#endif
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      goto err1;
    }
  }

  // 开始 分配超页
  for(; a + SUPERPGSIZE <= newsz; a += SUPERPGSIZE){
    void *spa = superalloc();
    if(spa == 0){
      // 若分配 superpage 失败/ no superpage ，分配 4 KB
      for(uint64 a_a = a; a_a < newsz && a_a < a + SUPERPGSIZE; a_a += PGSIZE){
        mem = kalloc();
        if(mem == 0) goto err1;
#ifndef LAB_SYSCALL
        memset(mem, 0, PGSIZE);
#endif
        if(mappages(pagetable, a_a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
          kfree(mem);
          goto err1;
        }
      }
      continue;
    }
    if(mapSuperpages(pagetable, a, SUPERPGSIZE, (uint64)spa, PTE_R|PTE_U|xperm) != 0){
      superfree(spa);
      goto err1;
    } 
  }

  
  // 剩下不足 2 MB 的部分 分配普通 页
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0) goto err1;
#ifndef LAB_SYSCALL
    memset(mem, 0, PGSIZE);
#endif
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      goto err1;
    }
  }


  return newsz;

err1:
  // 释放已经分配的内存到 oldsz
  uvmdealloc(pagetable, a, oldsz);
  return 0;
}

// // Deallocate user pages to bring the process size from oldsz to
// // newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// // need to be less than oldsz.  oldsz can be larger than the actual
// // process size.  Returns the new process size.
// uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
//   if (newsz >= oldsz)
//     return oldsz;

//   if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
//     int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
//     uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
//   }

//   return newsz;
// }

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;
  // 现在是支持普通page
  // 需要修改为支持超页并在超页大小减少时重映到普通page

  //uint64 a = PGROUNDUP(newsz);
  //uint64 end = PGROUNDUP(oldsz);

  if(oldsz - newsz < 512 *PGSIZE){
    if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
      int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
      uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
    }
  }
  else if(oldsz - newsz >= 512 * PGSIZE){
    // 判断逻辑即 如果 newsz 大于 2 MB，即只取消映射与 oldsz 相差的部分为普通页
    if (newsz >= SUPERPGSIZE) {
    // 仍然在 superpage 区间内 → superpage unmap
      if (SUPERPGROUNDDOWN(newsz) < SUPERPGROUNDDOWN(oldsz)) {
        int npages = (SUPERPGROUNDDOWN(oldsz) - SUPERPGROUNDDOWN(newsz)) / PGSIZE;
        uvmunmap(pagetable, SUPERPGROUNDDOWN(newsz), npages, 1);
      }
    }
      // 如果 newsz < 2 MB ，就直接全部映射为 普通页
    else {
      // newsz < 2MB，彻底退回普通页 → degrade
      if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
        int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
        uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
      }
    }
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable) {
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if (pte & PTE_V) {
      // backtrace();
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz) {
  if (sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}

// // Given a parent process's page table, copy
// // its memory into a child's page table.
// // Copies both the page table and the
// // physical memory.
// // returns 0 on success, -1 on failure.
// // frees any allocated pages on failure.
// int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
//   pte_t *pte;
//   uint64 pa, i;
//   uint flags;
//   char *mem;
//   int szinc = PGSIZE;

//   for (i = 0; i < sz; i += szinc) {
//     if ((pte = walk(old, i, 0)) == 0)
//       continue;
//     if ((*pte & PTE_V) == 0) {
//       continue;
//     }
//     szinc = PGSIZE;
//     pa = PTE2PA(*pte);
//     flags = PTE_FLAGS(*pte);
//     if ((mem = kalloc()) == 0)
//       goto err;
//     memmove(mem, (char *)pa, PGSIZE);
//     if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0) {
//       kfree(mem);
//       goto err;
//     }
//   }
//   return 0;

// err:
//   uvmunmap(new, 0, i / PGSIZE, 1);
//   return -1;
// }

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
  pte_t *pte;
  uint64 va, pa;
  uint flags;
  char *mem;
  // int szinc = PGSIZE;
  // int szsuper = SUPERPGSIZE;

  for (va = 0; va < sz; va += PGSIZE) {
    pte = walk_level(old, va, 1, 0);
    if (pte && PTE_LEAF(*pte)) {
      uint64 superStart_va = SUPERPGROUNDDOWN(va);

      if (superStart_va != va)
        continue;
      uint64 pa = PTE2PA(*pte);
      uint64 paFLAG = PTE_FLAGS(*pte);
      void *child_pa = superalloc();
      if (!child_pa) {
        // 如果没有分配到 superpage 则将 parent 进程这一页表复制到 512 个 普通
        // page 上
        if (degradeSuperpage(old, superStart_va) < 0)
          return -1;
        continue;
      }
      memmove(child_pa, (void *)pa, SUPERPGSIZE);
      // 在新页表中 install level 1
      pte_t *newpte = walk_level(new, superStart_va, 1, 1);
      *newpte = PA2PTE((uint64)child_pa) | paFLAG;
      continue;
    }

    // 即 level 1 不是叶子页的情况
    if ((pte = walk(old, va, 0)) == 0)
      continue;
    if ((*pte & PTE_V) == 0) {
      continue;
    }
    // int szinc = PGSIZE;
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if ((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char *)pa, PGSIZE);
    if (mappages(new, va, PGSIZE, (uint64)mem, flags) != 0) {
      kfree(mem);
      goto err;
    }
  }

  return 0;

err:
  uvmunmap(new, 0, va / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void uvmclear(pagetable_t pagetable, uint64 va) {
  pte_t *pte;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
  uint64 n, va0, pa0;
  pte_t *pte;

  while (len > 0) {
    va0 = PGROUNDDOWN(dstva);
    if (va0 >= MAXVA)
      return -1;

    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) {
      if ((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }

    if ((pte = walk(pagetable, va0, 0)) == 0) {
      // printf("copyout: pte should exist %lx %ld\n", dstva, len);
      return -1;
    }

    // forbid copyout over read-only user text pages.
    if ((*pte & PTE_W) == 0)
      return -1;

    n = PGSIZE - (dstva - va0);
    if (n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len) {
  uint64 n, va0, pa0;

  while (len > 0) {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) {
      if ((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }
    n = PGSIZE - (srcva - va0);
    if (n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max) {
  uint64 n, va0, pa0;
  int got_null = 0;

  while (got_null == 0 && max > 0) {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if (n > max)
      n = max;

    char *p = (char *)(pa0 + (srcva - va0));
    while (n > 0) {
      if (*p == '\0') {
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if (got_null) {
    return 0;
  } else {
    return -1;
  }
}

// allocate and map user memory if process is referencing a page
// that was lazily allocated in sys_sbrk().
// returns 0 if va is invalid or already mapped, or if
// out of physical memory, and physical address if successful.
uint64 vmfault(pagetable_t pagetable, uint64 va, int read) {
  uint64 mem;
  struct proc *p = myproc();

  if (va >= p->sz)
    return 0;
  va = PGROUNDDOWN(va);
  if (ismapped(pagetable, va)) {
    return 0;
  }
  mem = (uint64)kalloc();
  if (mem == 0)
    return 0;
  memset((void *)mem, 0, PGSIZE);
  if (mappages(p->pagetable, va, PGSIZE, mem, PTE_W | PTE_U | PTE_R) != 0) {
    kfree((void *)mem);
    return 0;
  }
  return mem;
}

// int ismapped(pagetable_t pagetable, uint64 va) {
//   pte_t *pte = walk(pagetable, va, 0);
//   if (pte == 0) {
//     return 0;
//   }
//   if (*pte & PTE_V) {
//     return 1;
//   }
//   return 0;
// }

int ismapped(pagetable_t pagetable, uint64 va) {
  // 超页
  pte_t *pte1 = walk_level(pagetable, va, 1, 0);
  pte_t *pte0;
  if (pte1 && PTE_LEAF(*pte1)) {
    return 1;
  }

  // 判断普通页是否被map
  pte0 = walk(pagetable, va, 0);
  if (pte0 == 0) {
    return 0;
  }
  if (*pte0 & PTE_V) {
    return 1;
  }

  return 0;
}

#ifdef LAB_PGTBL
pte_t *pgpte(pagetable_t pagetable, uint64 va) {
  return walk(pagetable, va, 0);
}
#endif
