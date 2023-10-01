#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "syscall.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0; 

  // 如果trap来自kernel，则panic，其中的 r,w prefix indicate read and write
  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  // 将 stvec trap vector 指向 kernelvec
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  // 将 sepc 保存的原来进程的PC值载入到kernel->trapframe->epc中
  p->trapframe->epc = r_sepc();
  
  // 相当于用mask来判断是否为syscall, 8 for syscall
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    // 将载入的原进程的PC指向下一条指令，在返回时就能继续执行指令流
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.

    // 可以 gd 进去看看其定义, w_sstatus(r_sstatus | SIE)
    // 开启设备中断
    // 为什么要开启设备中断呢？是允许由系统调用通过访问控制设备寄存器来与设备进行交互吗？ 
    intr_on();
    // 开始执行系统调用
    syscall();
  } else if((which_dev = devintr()) != 0){
    // returns 2 if timer interrupt,
            // 1 if other device,
            // 0 if not recognized.
    // ok
  } else { // 如果 which_dev == 0，就会执行以下代码
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2){
    // 通过 epc 来往返 user 和 kernel区，因为我们在 kernel 不能直接调用
    // user 的函数
    p->last_tick += 1;
    
    if (p->interval != 0){
      if (p->last_tick == p->interval && !p->alarm_flag){
        *p->alarmframe = *p->trapframe;
        p->trapframe->epc = (uint64)p->fn;
        p->last_tick = 0;
        p->alarm_flag = 1;
      }
    }

    yield();
  }
  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  // 关闭设备中断，因为我们将要让 stvec 指向 uservec，但我们当前还没有返回用户空间，
  // 发生的中断却会从 uservec开始处理，所以我们得在设置 stvec 前关闭中断。
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  // 使 stvec 寄存器指向 uservec
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  // 保存我们之前在 trampoline 中恢复的寄存器/状态，使得用户进程下次发生trap时
  // 有相关预先设置好的信息
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  // sstatus 的 SPP位在 sret 指令中会控制返回的模式，我们将其设置为用户模式
  // SPIE位则控制中断的开关，在我们通过sret指令返回用户模式后，希望中断能发生
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  // 将在usertrap中保存的epc加载到sepc寄存器中，该用户程序在返回后，
  // sret会将epc的内容复制到pc中。
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  // 跳转到 trampoline.S, 比且将切换到用户页表，恢复寄存器，通过sret切换到用户模式
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  // 这是怎么 invoking 的？TRAPFRAME是一个虚拟地址，*表示取值？然后就开始汇编之旅？
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

