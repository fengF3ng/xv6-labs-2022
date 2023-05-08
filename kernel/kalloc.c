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
} kmem;

struct {
  struct spinlock lock;
  int count[PHYSTOP / PGSIZE];
} ref;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&ref.lock, "ref");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
  {
    ref.count[(uint64)p / PGSIZE] = 1;
    kfree(p);
  }
    
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

#ifdef LAB_COW
  acquire(&ref.lock);
  if (--ref.count[(uint64)pa / PGSIZE] == 0) {
    release(&ref.lock);
  } else {
    release(&ref.lock);
    return ;
  }
#endif

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
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

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
#ifdef LAB_COW
  if(r){
    acquire(&ref.lock);
    ref.count[(uint64)r / PGSIZE] = 1;
    release(&ref.lock);
  }

#endif
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

#ifdef LAB_COW
void
refcount_inc(void *pa)
{
  acquire(&ref.lock);
  ref.count[(uint64)pa / PGSIZE]++;
  release(&ref.lock);
}

int cowpage(pagetable_t pagetable, uint64 va) 
{
  if(va >= MAXVA)
    return -1;

  pte_t* pte = walk(pagetable, va, 0);
  if(pte == 0)
    return -1;

  uint flags = PTE_FLAGS(*pte);
  if((flags & PTE_V) == 0)
    return -1;
  if(flags & PTE_COW)
    return 0;

  return -1;
}

void* cowalloc(pagetable_t pagetable, uint64 va) 
{
  if(PGROUNDDOWN(va) != va)
    return 0;

  pte_t *pte;
  uint64 pa;
  if((pte = walk(pagetable, va, 0)) == 0)
    panic("cowalloc: pte should exist");
  pa = PTE2PA(*pte);

  if(ref.count[pa / PGSIZE] == 1) {
    *pte |= PTE_W;
    *pte &= ~PTE_COW;
    return (void*)pa;
  } else {
    char* mem;
    if((mem = kalloc()) == 0)
      return 0;
    memmove(mem, (char*)pa, PGSIZE);

    // clear PTE_V, otherwise mappagges will see it as remap
    *pte &= ~PTE_V;

    if(mappages(pagetable, va, PGSIZE, (uint64)mem, (PTE_FLAGS(*pte) | PTE_W) & ~PTE_COW) != 0) {
      kfree(mem);
      *pte |= PTE_V;
      return 0;
    }

    kfree((char*)PGROUNDDOWN(pa));
    return mem;
  }
}
#endif