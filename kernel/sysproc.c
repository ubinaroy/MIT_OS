#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.

  uint64 uaddr;
  // 现在 uaddr 承载着需要检查的那些pages的第一页
  argaddr(0, &uaddr);

  // 页数
  int npages;
  argint(1, &npages);

  // 这里我看大很多博主都加了这样一个页数限制，那我也加一个
  if (npages > 64){
    printf("too many pages\n");
    exit(-1);
  }

  // Buffer to store the Accessed addrs
  uint64 buffer;
  uint64 bitmask = 0;
  argaddr(2, &buffer);

  pagetable_t pgtbl = myproc()->pagetable;
  
  // 根据 PTE 的 PTE_V 来判断当前虚拟地址是否 Acessed
  for (int i = 0; i < npages; i ++){
    pte_t *pte = walk(pgtbl, uaddr + i * PGSIZE, 0);
    // 如果pte无效或未被访问
    if ((!pte) || !(*pte & PTE_A))
      continue;
    // 将有效的 | 入bitmask里
    bitmask = bitmask | (1 << i);
    *pte = *pte & (~PTE_A);
  }
  // 将AC的存入buffer中, 我们必须通过copyout或者说是基于pa的memmove实现
  copyout(pgtbl, buffer, (char *)(&bitmask), sizeof(uint64));
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
