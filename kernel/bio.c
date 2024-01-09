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

#define BUCKETSZ 13

struct {
  struct buf buf[NBUF];

  struct BucketType {
    struct spinlock lock;
    struct buf head;
  } buckets[BUCKETSZ];
} bcache;

inline int Hash(int dev, int blockno) {
  return (dev + blockno) % BUCKETSZ;
}

void
binit(void)
{
  for (int i = 0; i < BUCKETSZ; i ++) {
    char names[9];
    snprintf(names, 9, "bcache_%d", i);
    initlock(&bcache.buckets[i].lock, names);
  }

  for (int i = 0; i < NBUF; i ++)
    bcache.buf[i].timestamp = ticks;

  for (int i = 0; i < BUCKETSZ; i ++)
    bcache.buckets[i].head.next = 0;

  for (struct buf *b = bcache.buf; b < bcache.buf + NBUF; b ++) {
    b->next = bcache.buckets[0].head.next;
    initsleeplock(&b->lock, "buffer");
    bcache.buckets[0].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  int key = Hash(dev, blockno); // 哈希
  struct BucketType *bucket = &bcache.buckets[key];

  acquire(&bucket->lock);

  // Is the block already cached?
  for(struct buf* b = bucket->head.next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bucket->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bucket->lock);
  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.

  struct buf* before_least = 0;
  uint least_time = 0xffffffff;
  struct BucketType* least_bucket = 0;

  for (int i = 0; i < BUCKETSZ; i ++) {
    struct BucketType* cur_bucket = &bcache.buckets[i];
    if (!cur_bucket) continue;
    acquire(&cur_bucket->lock);

    uint flag = 0;

    for (struct buf* b = &cur_bucket->head; b->next; b = b->next) {
      // 我们查找的是 lru 的前一个节点，因为我们需要将其删除
      if (b->next->refcnt == 0 && b->next->timestamp < least_time) {
        before_least = b;
        least_time = before_least->timestamp;
        flag = 1;
      }
    }

    if(!flag) {
      // 未找到更优，直接释放着找下一个 bucket
      release(&cur_bucket->lock);
    }
    else {
      // 这里需要一直获取 lru 所在的那个 bucket 的锁，在循环外面释放
      // 因为如果是并行的话，可能存在同时多个线程都执行 bget，或许到同一个 dev 和 blockno 对应的 buf
      // 直接释放最优的锁会破坏整个 "删除-添加" 的原子性
      if(least_bucket != 0) release(&least_bucket->lock);
      least_bucket = cur_bucket;
    }
  }

  if (!before_least)
    panic("bget: no buffers");

  // 在最优 bucket 内删除该 lru buffer
  struct buf* lru = before_least->next;
  before_least->next = lru->next;

  release(&least_bucket->lock);

  acquire(&bucket->lock);

  // double check，是否存在 lru 被重复冲刷到本 bucket 里了，是的话直接返回
  // double check 可以将在某不知名线程冲刷之后，重新有了 cache，就直接读取
  for (struct buf* b = bucket->head.next; b; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt ++;
      release(&bucket->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  lru->next = bucket->head.next;
  bucket->head.next = lru;

  lru->dev = dev;
  lru->blockno = blockno;
  lru->refcnt = 1;
  lru->valid = 0;
  release(&bucket->lock);

  acquiresleep(&lru->lock);
  return lru;
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
  
  uint key = Hash(b->dev, b->blockno);

  acquire(&bcache.buckets[key].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->timestamp = ticks;
  }
  
  release(&bcache.buckets[key].lock);
}

void
bpin(struct buf *b) {
  uint key = Hash(b->dev, b->blockno);
  acquire(&bcache.buckets[key].lock);
  b->refcnt++;
  release(&bcache.buckets[key].lock);
}

void
bunpin(struct buf *b) {
  uint key = Hash(b->dev, b->blockno);
  acquire(&bcache.buckets[key].lock);
  b->refcnt--;
  release(&bcache.buckets[key].lock);}


