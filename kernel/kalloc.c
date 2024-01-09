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

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  for (int i = 0; i < NCPU; i ++) {
    char names[8];
    snprintf(names, 7, "kmem_%d", i);
    initlock(&kmem[i].lock, names);
  }

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

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int id = cpuid();
  pop_off();

  acquire(&kmem[id].lock);

  r->next = kmem[id].freelist;
  kmem[id].freelist = r;

  release(&kmem[id].lock);

}

// steal free page from other cpus
struct run *
steal(int cpu_id)
{
  struct run* head = 0;
  int page_i = 0, pgsz =  16;

  for (int next_cpu = 0; next_cpu < NCPU; next_cpu ++)
  {
    if (next_cpu == cpu_id) continue;
    acquire(&kmem[next_cpu].lock);
    struct run *r = kmem[next_cpu].freelist;

    while (page_i < pgsz && r) {
      kmem[next_cpu].freelist = r->next;
      r->next = head;
      head = r;
      r = kmem[next_cpu].freelist;
      page_i ++;

      if (page_i == 16) {
        release(&kmem[next_cpu].lock);
        return head;
      }
    }

    release(&kmem[next_cpu].lock);
  }
  return head;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int cpu_id = cpuid();
  pop_off();

  acquire(&kmem[cpu_id].lock);
 
  r = kmem[cpu_id].freelist;
  if(r) {
    kmem[cpu_id].freelist = r->next;
    // release(&kmem[cpu_id].lock);
  }
  else { // allocate failed, we need to steal some page from neighbors
    // release(&kmem[cpu_id].lock);

    r = steal(cpu_id);

    if (r) {
      // acquire(&kmem[cpu_id].lock);
      kmem[cpu_id].freelist = r->next;
      // release(&kmem[cpu_id].lock);
    }
  }
  release(&kmem[cpu_id].lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
