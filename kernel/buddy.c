#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

// Buddy allocator

static int nsizes;     // the number of entries in bd_sizes array

#define LEAF_SIZE     16                         // The smallest block size
#define MAXENTRY       (nsizes-1)                 // Largest index in bd_sizes array
#define BLK_SIZE(k)   ((1L << (k)) * LEAF_SIZE)  // Size of block at size k
#define HEAP_SIZE     BLK_SIZE(MAXENTRY) 
#define NBLK(k)       (1 << (MAXENTRY-k))         // Number of block at size k
#define ROUNDUP(n,sz) (((((n)-1)/(sz))+1)*(sz))  // Round up to the next multiple of sz
#define LEFT_CHILD(i)  2*(i)
#define RIGHT_CHILD(i) (2*(i)+1)
#define PARENT(i)      ((i)/2)

typedef struct list Bd_list;

// The allocator has sz_info for each size k. Each sz_info has a free
// list, an array alloc to keep track which blocks have been
// allocated, and an split array to to keep track which blocks have
// been split.  The arrays are of type char (which is 1 byte), but the
// allocator uses 1 bit per block (thus, one char records the info of
// 8 blocks).
struct sz_info {
  // Bd_list free;
  char *alloc;
  char *split;
};
typedef struct sz_info Sz_info;

static Sz_info *bd_sizes; 
static void *bd_base;   // start address of memory managed by the buddy allocator
static struct spinlock lock;

// Return 1 if bit at position index in array is set to 1
int isset(char *array, int index) {
  char b = array[index/8];
  char m = (1 << (index % 8));
  return (b & m) == m;
}

// Set bit at position index in array to 1
void set(char *array, int index) {
  char b = array[index/8];
  char m = (1 << (index % 8));
  array[index/8] = (b | m);
}

// Clear bit at position index in array
void unset(char *array, int index) {
  char b = array[index/8];
  char m = (1 << (index % 8));
  array[index/8] = (b & ~m);
}


// What is the first k such that 2^k >= n?
int
get_level(uint64 n) {
  int k = 0;
  uint64 size = LEAF_SIZE;

  while (size < n) {
    k++;
    size *= 2;
  }
  return k;
}

// Compute the block index for address p at size k
int
blk_index(int k, char *p) {
  int n = p - (char *) bd_base;
  return n / BLK_SIZE(k);
}

// Convert a block index at size k back into an address
void *addr(int k, int bi) {
  int n = bi * BLK_SIZE(k);
  return (char *) bd_base + n;
}


void print_level(int level){

  printf("level %d (block size %d, num of blocks %d): ", level, BLK_SIZE(level), NBLK(level));
  unsigned alls = 0, splits = 0, frees = 0;
  printf("Allocated blocks: ");
  
  // show which block is marked as allocated on this level
  for (unsigned i = 0; i < NBLK(level); i++) {
    // print in such a manner:
    /*
    ** idx idx idx
    **  S   A   F
    */
    if ((level != 0) && isset(bd_sizes[level].split, i)) {
      printf("S ");
      splits++;
    }
    else if (isset(bd_sizes[level].alloc, i)) {
      printf("A ");
      alls++;
    }
    else {
      printf("F ");
      frees++;
    }
  }
  printf("\n");
  printf("Allocated: %d, Split: %d, Free: %d\n", alls, splits, frees);
}

void bd_show_memory(){
  // print the memory scale
  printf("bd: memory scale\n");
  for (int k = 0; k < nsizes; k++) {
    print_level(k);
    // show which block is marked as allocated on this level
  }
}


void set_blocks_below_as_allocated(int level, char* p){
  if (level == 0) {
    return;
  }
  int current_id = blk_index(level, p);
  set(bd_sizes[level-1].alloc, LEFT_CHILD(current_id));
  set(bd_sizes[level-1].alloc, RIGHT_CHILD(current_id));
  set_blocks_below_as_allocated(level-1, addr(level-1, LEFT_CHILD(current_id)));
  set_blocks_below_as_allocated(level-1, addr(level-1, RIGHT_CHILD(current_id)));
}

void set_blocks_above_as_split(int level, char* p){
  if (level == MAXENTRY) {
    return;
  }
  int current_id = blk_index(level, p);
  set(bd_sizes[level+1].split, PARENT(current_id));
  set_blocks_above_as_split(level+1, addr(level+1, PARENT(current_id)));
}

void unset_blocks_below_as_allocated(int level, char* p){
  if (level == 0) {
    return;
  }
  int current_id = blk_index(level, p);
  unset(bd_sizes[level-1].alloc, LEFT_CHILD(current_id));
  unset(bd_sizes[level-1].alloc, RIGHT_CHILD(current_id));
  unset_blocks_below_as_allocated(level-1, addr(level-1, LEFT_CHILD(current_id)));
  unset_blocks_below_as_allocated(level-1, addr(level-1, RIGHT_CHILD(current_id)));
}


// allocate nbytes, but malloc won't return anything smaller than LEAF_SIZE
void *
bd_malloc(uint64 nbytes)
{ 
  printf("buddy system: allocating %d bytes\n", nbytes);
  int level;

  acquire(&lock);
  printf("lock acquired\n");

  // Find a free block >= nbytes, starting with smallest k possible
  level = get_level(nbytes);
  printf("The smallest k possible is %d\n", level);
  unsigned id = 0;

  if (level >= nsizes) {
    printf("We did not find any free block\n");
    release(&lock);
    return 0;
  } else if (level == 0) {
      for (; id < NBLK(level); id++) {
        // printf("Checking the %dth block at level %d\n", id, level);
        if (!isset(bd_sizes[level].alloc, id)) {
          break;
        }
      }
  } else{
      for (; id < NBLK(level); id++) {
        // printf("Checking the %dth block at level %d\n", id, level);
        if (!isset(bd_sizes[level].alloc, id) && !isset(bd_sizes[level].split, id)) {
          break;
        }
      }
  }

  if (id == NBLK(level)) {
    printf("We did not find any free block\n");
    release(&lock);
    return 0;
  }

  printf("We found a free block at level %d\n", level);
  char *p = addr(level, id);

  printf("We found a free block at level %d, it's the %dth out of %d blocks on that level, it's %d bytes.\n", level, id, NBLK(level) ,BLK_SIZE(level));
  set(bd_sizes[level].alloc, id);

  // Mark all the blocks below this level as allocated
  set_blocks_below_as_allocated(level, p);

  // Mark all the blocks above as split
  set_blocks_above_as_split(level, p);

  release(&lock);
  printf("lock released\n");

  return p;
}

// Find the size of the block that p points to.
int
size(char *p) {
  for (int k = 0; k < nsizes; k++) {
    if(isset(bd_sizes[k+1].split, blk_index(k+1, p))) { // 如果高层split了，说明这个block是k的
      return k;
    }
  }
  return 0;
}

// Free memory pointed to by p, which was earlier allocated using
// bd_malloc.
void
bd_free(void *p) {
  if (p == 0)
    return;
  else if ((uint64)p % LEAF_SIZE != 0)
    panic("bd_free: not aligned");

  void *q;
  int k;
  printf("buddy system: freeing %p\n", p);
  acquire(&lock);

  unsigned level_free = size(p);
  unsigned block_size_free = BLK_SIZE(level_free);
  unsigned current_id = blk_index(level_free, p);

  printf("This is a block of %d bytes, it is the %dth block on the %dth level\n", block_size_free, current_id, level_free);

  printf("To free this block, we begin from its level up to the max level, combining buddies if possible\n");
  for (k = level_free; k < MAXENTRY; k++) {
    int bi = blk_index(k, p);
    int buddy = (bi % 2 == 0) ? bi+1 : bi-1;
    unset(bd_sizes[k].alloc, bi);  // free p at size k
    printf("Freeing at level %d. Unset the alloc bit for the %dth block\n", k, bi);

    if (k == 0){
      if (isset(bd_sizes[k].alloc, buddy)) {  // is buddy allocated?
        printf("Buddy is not free, we can't merge, freeing terminated.\n");
        break;   // break out of loop
      }
    }
    else{
      if (isset(bd_sizes[k].alloc, buddy) || isset(bd_sizes[k].split, buddy)) {  // is buddy allocated?
        printf("Buddy is not free, we can't merge, freeing terminated.\n");
        break;   // break out of loop
      }
    }

    printf("Buddy is free, merge ");
    // budy is free; merge with buddy
    q = addr(k, buddy);

    if(buddy % 2 == 0) {
      printf("with right buddy\n");
      p = q;
    }
    else {
      printf("with left buddy\n");
    }

    // unset all the blocks below this level as allocated, do later
    // unset the block above as split
    unset(bd_sizes[k+1].split, blk_index(k+1, p));

    printf("Unset the split bit for the %dth block at level %d\n", blk_index(k+1, p), k+1);
  }

  unset_blocks_below_as_allocated(k, p);

  release(&lock);
}

// Compute the first block at size k that doesn't contain p
int
blk_index_next(int k, char *p) {
  int n = (p - (char *) bd_base) / BLK_SIZE(k);
  if((p - (char*) bd_base) % BLK_SIZE(k) != 0)
      n++;
  return n ;
}

int
log2(uint64 n) {
  int k = 0;
  while (n > 1) {
    k++;
    n = n >> 1;
  }
  return k;
}

// Mark memory from [start, stop), starting at size 0, as allocated. 
void
bd_mark(void *start, void *stop)
{
  int bi, bj;

  if (((uint64) start % LEAF_SIZE != 0) || ((uint64) stop % LEAF_SIZE != 0))
    panic("bd_mark");

  for (int k = 0; k < nsizes; k++) {
    bi = blk_index(k, start);
    bj = blk_index_next(k, stop);
    for(; bi < bj; bi++) {
      if(k > 0) {
        // if a block is allocated at size k, mark it as split too.
        set(bd_sizes[k].split, bi);
      }
      set(bd_sizes[k].alloc, bi);
    }
  }
}

// Mark the range [bd_base,p) as allocated
int
bd_mark_data_structures(char *p) {
  int meta = p - (char*)bd_base;
  printf("bd: %d meta bytes for managing %d bytes of memory\n", meta, BLK_SIZE(MAXENTRY));
  bd_mark(bd_base, p);
  return meta;
}

// Mark the range [end, HEAPSIZE) as allocated
int
bd_mark_unavailable(void *end, void *left) {
  int unavailable = BLK_SIZE(MAXENTRY)-(end-bd_base);
  if(unavailable > 0)
    unavailable = ROUNDUP(unavailable, LEAF_SIZE);
  printf("bd: 0x%x bytes unavailable\n", unavailable);

  void *bd_end = bd_base+BLK_SIZE(MAXENTRY)-unavailable;
  bd_mark(bd_end, bd_base+BLK_SIZE(MAXENTRY));
  return unavailable;
}

// Initialize the buddy allocator: it manages memory from [base, end).
void
bd_init(void *base, void *end) {
  char *p = (char *) ROUNDUP((uint64)base, LEAF_SIZE);
  int sz;

  initlock(&lock, "buddy");
  bd_base = (void *) p;

  // compute the number of sizes we need to manage [base, end)
  nsizes = log2(((char *)end-p)/LEAF_SIZE) + 1;
  if((char*)end-p > BLK_SIZE(MAXENTRY)) {
    nsizes++;  // round up to the next power of 2
  }

  printf("bd: memory sz is %d bytes; allocate an size array of length %d\n",
         (char*) end - p, nsizes);

  // allocate bd_sizes array
  bd_sizes = (Sz_info *) p;
  p += sizeof(Sz_info) * nsizes;
  memset(bd_sizes, 0, sizeof(Sz_info) * nsizes);

  // initialize alloc array for each size k
  for (int k = 0; k < nsizes; k++) {
    sz = sizeof(char)* ROUNDUP(NBLK(k), 8)/8;
    bd_sizes[k].alloc = p;
    memset(bd_sizes[k].alloc, 0, sz); // set all blocks as free
    p += sz;
  }

  // allocate the split array for each size k, except for k = 0, since
  // we will not split blocks of size k = 0, the smallest size.
  for (int k = 1; k < nsizes; k++) {
    sz = sizeof(char)* (ROUNDUP(NBLK(k), 8))/8;
    bd_sizes[k].split = p;
    memset(bd_sizes[k].split, 0, sz); // set all blocks as not split
    p += sz;
  }
  p = (char *) ROUNDUP((uint64) p, LEAF_SIZE);

  // mark our data management part as allocated.
  bd_mark_data_structures(p);
  
  // mark the unavailable memory range [end, HEAP_SIZE) as allocated,
  // so that buddy will not hand out that memory.
  int unavailable = bd_mark_unavailable(end, p);
  void *bd_end = bd_base+BLK_SIZE(MAXENTRY)-unavailable;
  
  printf("Actual usable memory: %d\n", (uint64)bd_end - (uint64)p);

  print_level(10);

}
