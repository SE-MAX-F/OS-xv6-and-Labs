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

#define NBUCKET 13

struct bucket {
  struct spinlock lock;
  struct buf *head;
};

struct {
  struct spinlock lock;
  struct bucket bucket[NBUCKET];
  struct buf buf[NBUF];
} bcache;

static int
bhash(uint dev, uint blockno)
{
  return (dev + blockno) % NBUCKET;
}

static void
bucket_insert(struct bucket *bucket, struct buf *b)
{
  b->prev = 0;
  b->next = bucket->head;

  if(bucket->head)
    bucket->head->prev = b;

  bucket->head = b;
}

static void
bucket_remove(struct bucket *bucket, struct buf *b)
{
  if(b->prev)
    b->prev->next = b->next;
  else
    bucket->head = b->next;

  if(b->next)
    b->next->prev = b->prev;

  b->prev = 0;
  b->next = 0;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  for(int i = 0; i < NBUCKET; i++){
    initlock(&bcache.bucket[i].lock, "bcache.bucket");
    bcache.bucket[i].head = 0;
  }

  for(int i = 0; i < NBUF; i++){
    b = &bcache.buf[i];

    initsleeplock(&b->lock, "buffer");

    b->valid = 0;
    b->disk = 0;
    b->dev = (uint)-1;
    b->blockno = (uint)-1;
    b->refcnt = 0;
    b->lastuse = 0;

    bucket_insert(&bcache.bucket[i % NBUCKET], b);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;
  int target = bhash(dev, blockno);

  // Fast path: search the target bucket.
  acquire(&bcache.bucket[target].lock);

  for(b = bcache.bucket[target].head; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket[target].lock);

      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.bucket[target].lock);

  // Serialize cache misses and buffer eviction.
  acquire(&bcache.lock);

  // Recheck after acquiring the global eviction lock.
  acquire(&bcache.bucket[target].lock);

  for(b = bcache.bucket[target].head; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket[target].lock);
      release(&bcache.lock);

      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.bucket[target].lock);

  for(;;){
    struct buf *victim = 0;
    int victim_bucket = -1;
    uint oldest = 0;

    // Find the least recently used unused buffer.
    for(int i = 0; i < NBUCKET; i++){
      acquire(&bcache.bucket[i].lock);

      for(struct buf *x = bcache.bucket[i].head;
          x != 0;
          x = x->next){
        if(x->refcnt == 0 &&
           (victim == 0 || x->lastuse < oldest)){
          victim = x;
          victim_bucket = i;
          oldest = x->lastuse;
        }
      }

      release(&bcache.bucket[i].lock);
    }

    if(victim == 0)
      panic("bget: no buffers");

    // Recheck the victim because another CPU may have started using it.
    acquire(&bcache.bucket[victim_bucket].lock);

    if(victim->refcnt != 0){
      release(&bcache.bucket[victim_bucket].lock);
      continue;
    }

    // Move the buffer if the new block belongs to another bucket.
    if(victim_bucket != target)
      acquire(&bcache.bucket[target].lock);

    bucket_remove(&bcache.bucket[victim_bucket], victim);

    victim->dev = dev;
    victim->blockno = blockno;
    victim->valid = 0;
    victim->refcnt = 1;
    victim->lastuse = 0;

    bucket_insert(&bcache.bucket[target], victim);

    if(victim_bucket != target)
      release(&bcache.bucket[target].lock);

    release(&bcache.bucket[victim_bucket].lock);
    release(&bcache.lock);

    acquiresleep(&victim->lock);
    return victim;
  }
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
  int bucket;

  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  bucket = bhash(b->dev, b->blockno);

  acquire(&bcache.bucket[bucket].lock);

  b->refcnt--;

  if(b->refcnt == 0){
    acquire(&tickslock);
    b->lastuse = ticks;
    release(&tickslock);
  }

  release(&bcache.bucket[bucket].lock);
}

void
bpin(struct buf *b)
{
  int bucket = bhash(b->dev, b->blockno);

  acquire(&bcache.bucket[bucket].lock);
  b->refcnt++;
  release(&bcache.bucket[bucket].lock);
}

void
bunpin(struct buf *b)
{
  int bucket = bhash(b->dev, b->blockno);

  acquire(&bcache.bucket[bucket].lock);
  b->refcnt--;
  release(&bcache.bucket[bucket].lock);
}

