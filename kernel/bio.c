// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#ifdef LAB_LOCK
struct buckethead {
  struct buf buf;
  struct spinlock lock;
};
#endif
struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
#ifdef LAB_LOCK
  struct buckethead bucket[NBUCKET];
#else
  struct buf head;
#endif
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
#ifdef LAB_LOCK
  for(int bucketid = 0; bucketid < NBUCKET; bucketid++) {
    initlock(&bcache.bucket[bucketid].lock, "bcache.bucket");

    bcache.bucket[bucketid].buf.prev = &bcache.bucket[bucketid].buf;
    bcache.bucket[bucketid].buf.next = &bcache.bucket[bucketid].buf;
  }
  for(b = bcache.buf; b < bcache.buf+NBUF; b++) {
    b->next = bcache.bucket[0].buf.next;
    b->prev = &bcache.bucket[0].buf;
    initsleeplock(&b->lock, "buffer");
    bcache.bucket[0].buf.next->prev = b;
    bcache.bucket[0].buf.next = b;
  }
#else
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
#endif
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

#ifndef LAB_LOCK
  acquire(&bcache.lock);
#endif

  // Is the block already cached?
#ifdef LAB_LOCK
  int current_bucketid = blockno % NBUCKET;
  acquire(&bcache.bucket[current_bucketid].lock);
  for(b = bcache.bucket[current_bucketid].buf.next; b != &bcache.bucket[current_bucketid].buf; b = b->next){
#else
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
#endif
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
#ifdef LAB_LOCK
      release(&bcache.bucket[current_bucketid].lock);
#else
      release(&bcache.lock);
#endif
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
#ifdef LAB_LOCK
  // search from current bucket, avoiding starve
  for(int bucketid = current_bucketid, firstin = 1; bucketid != current_bucketid || firstin; bucketid = (bucketid + 1) % NBUCKET, firstin = 0) {
    if(bucketid != current_bucketid)
      acquire(&bcache.bucket[bucketid].lock);

  for(b = bcache.bucket[bucketid].buf.prev; b != &bcache.bucket[bucketid].buf; b = b->prev){
#else
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
#endif
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
#ifdef LAB_LOCK
      // steal it from other bucket
      b->next->prev = b->prev;
      b->prev->next = b->next;
      if(bucketid != current_bucketid)
        release(&bcache.bucket[bucketid].lock);
      // install it in current bucket
      b->next = bcache.bucket[current_bucketid].buf.next;
      b->prev = &bcache.bucket[current_bucketid].buf;
      bcache.bucket[current_bucketid].buf.next->prev = b;
      bcache.bucket[current_bucketid].buf.next = b;
      release(&bcache.bucket[current_bucketid].lock);
#else
      release(&bcache.lock);
#endif
      acquiresleep(&b->lock);
      return b;
    }
  }
#ifdef LAB_LOCK
  if(bucketid != current_bucketid)
    release(&bcache.bucket[bucketid].lock);
  }
#endif
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

#ifdef LAB_LOCK
  int current_bucketid = (b->blockno) % NBUCKET;
  acquire(&bcache.bucket[current_bucketid].lock);
#else
  acquire(&bcache.lock);
#endif
  b->refcnt--;
#ifdef LAB_LOCK
  release(&bcache.bucket[current_bucketid].lock);
#else
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
#endif
}

void
bpin(struct buf *b) {
#ifdef LAB_LOCK
  int current_bucketid = (b->blockno) % NBUCKET;
  acquire(&bcache.bucket[current_bucketid].lock);
#else
  acquire(&bcache.lock);
#endif
  b->refcnt++;
#ifdef LAB_LOCK
  release(&bcache.bucket[current_bucketid].lock);
#else
  release(&bcache.lock);
#endif
}

void
bunpin(struct buf *b) {
#ifdef LAB_LOCK
  int current_bucketid = (b->blockno) % NBUCKET;
  acquire(&bcache.bucket[current_bucketid].lock);
#else
  acquire(&bcache.lock);
#endif
  b->refcnt--;
#ifdef LAB_LOCK
  release(&bcache.bucket[current_bucketid].lock);
#else
  release(&bcache.lock);
#endif
}


