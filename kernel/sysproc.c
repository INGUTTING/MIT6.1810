#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#ifdef PGTBL_SOL
#include "riscv.h"
#endif
#include "vm.h"

uint64 sys_exit(void) {
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64 sys_getpid(void) {
  // struct proc *p = myproc();
  // // 先映射物理地址然后将 pid 写入 该内存中
  // // uint64 pysicalAdd = (uint64)kalloc();

  // struct usyscall *us = (struct usyscall*)kalloc();
  // if(us == 0){
  //   return -1;
  // }
  // // kalloc 返回的是内核使用的虚拟地址(对应实际物理地址)
  // us->pid = p->pid;
  // uint64 pa = (uint64)us - KERNBASE;
  // if(mappages(p->pagetable,USYSCALL,PGSIZE,pa,PTE_R | PTE_U) == 0){
  //   kfree(us);
  //   return -1;
  // }
  // // 写入 内存中 加入一个 usyscall 结构体
  // //struct usyscall *sysCalls;
  // //sysCalls->pid = p->pid;
  // // 进行优化，分配地址时就用 usyscall 结构体指针地址进行映射即可
  struct proc *p = myproc();
  (p->usyscallIndex)->pid = p->pid;
  return p->pid;
}

uint64 sys_fork(void) { return kfork(); }

uint64 sys_wait(void) {
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64 sys_sbrk(void) {
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0) {
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64 sys_pause(void) {
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

#ifdef LAB_PGTBL
int sys_pgpte(void) {
  uint64 va;
  struct proc *p;

  p = myproc();
  argaddr(0, &va);
  pte_t *pte = pgpte(p->pagetable, va);
  if (pte != 0) {
    return (uint64)*pte;
  }
  return 0;
}
#endif

#ifdef LAB_PGTBL
int sys_kpgtbl(void) {
  struct proc *p;

  p = myproc();
  vmprint(p->pagetable);
  return 0;
}
#endif

uint64 sys_kill(void) {
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
