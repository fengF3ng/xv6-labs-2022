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
#ifdef LAB_LOCK
} kmem[NCPU];
#else
} kmem;
#endif

void
kinit()
{
#ifdef LAB_LOCK
  for(int i=0; i < NCPU; i++)
    initlock(&kmem[i].lock, "kmem");
#else
  initlock(&kmem.lock, "kmem");
#endif
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
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

#ifdef LAB_LOCK
  push_off();
  int _cpuid = cpuid();
  acquire(&kmem[_cpuid].lock);
  r->next = kmem[_cpuid].freelist;
  kmem[_cpuid].freelist = r;
  release(&kmem[_cpuid].lock);
  pop_off();
#else
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
#endif
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

#ifdef LAB_LOCK
  push_off();
  int _cpuid = cpuid();
  acquire(&kmem[_cpuid].lock);
  r = kmem[_cpuid].freelist;
  if(r)
    kmem[_cpuid].freelist = r->next;
  else { // steal memory from other cpu
    for (int i=0; i<NCPU; i++) {
      if (i == _cpuid) continue;
      acquire(&kmem[i].lock);
      // steal
      r = kmem[i].freelist;
      if(r){
        kmem[i].freelist = r->next;
        release(&kmem[i].lock);
        break;
      }
      release(&kmem[i].lock);
    }
  }
  release(&kmem[_cpuid].lock);
  pop_off();
#else
  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);
#endif

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
