#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"


// buddy allocator


#define LEAF_SIZE 16
#define BLK_SIZE(level)   ((1L << (level)) * LEAF_SIZE) 
#define ROUNDUP(n,sz) (((((n)-1)/(sz))+1)*(sz))  // Round up to the next multiple of sz
#define IS_POWER_OF_2(n) ((n & (n - 1)) == 0)  // Check if n is power of 2
#define MAX(a,b) ((a) > (b) ? (a) : (b))  // Return the maximum of a and b
#define MAX_NODES ((1<<24)>>4)<<1   // Maximum number of nodes in the buddy system

#define LEFT_LEAF(index)  ((index) * 2 + 1)
#define RIGHT_LEAF(index) ((index) * 2 + 2)
#define PARENT(index) (((index) - 1) / 2)

static unsigned heapsize;  // 16MB for buddy allocator
static unsigned nodes;     // number of nodes in the buddy system
static unsigned levels;
static void *start;

struct buddy {
  struct spinlock lock;
  unsigned largest_possible_block[MAX_NODES];
};

// Convert a block index at size k back into an address
void *addr(int level, int bi) {
  int n = bi * BLK_SIZE(level);
  return (char *) bd_base + n;
}

// Compute the block index for address p
int blk_index(char *p) {
  int n = p - (char *) bd_base;
  return n / LEAF_SIZE;
}

int get_level(int index) {
    return (int) (log2(index + 1));
}

int
nearest_power_of_2(int n) {
    while(IS_POWER_OF_2(n) == 0) {
        n++;
    }
    return n;
}

void bd_print() {
    acquire(&bd.lock);
    for(int i = 0; i < nodes; i++) {
        printf("Node %d: %d\n", i, buddy.largest_possible_block[i]);
    }
    release(&bd.lock);
}

void bd_init(void *head, void* tail) {

    char*p = (char*) ROUNDUP((uint64)head, LEAF_SIZE);
    start = (void*)p;

    initlock(&bd.lock, "buddy");

    heapsize = (uint64)tail - (uint64)p;
    unsigned biggest_node = ROUNDUP(heapsize, LEAF_SIZE);
    unsigned lowest_level_nodes = nearest_power_of_2(ROUNDUP(heapsize, LEAF_SIZE)/LEAF_SIZE);
    nodes = (lowest_level_nodes << 1) - 1;
    

    // Initialize the largest possible block for each node
    for(int i = 0, int node_size = biggest_node; i < nodes; i++) {

        buddy.largest_possible_block[i] = biggest_node;
        if(IS_POWER_OF_2(i+1)) {
            node_size >>= 1;
        }
    }

}

int traverse(int index, int level, int target_size) {
    int left_child = LEFT_LEAF(index);
    int right_child = RIGHT_LEAF(index);
    int left_size = buddy.largest_possible_block[left_child];
    int right_size = buddy.largest_possible_block[right_child];

    if(level == 0) {
        return index;
    }
    if left_size == target_size {
        return left_child;
    }
    if right_size == target_size {
        return right_child;
    }

    if((left_size > target_size) && (right_size > target_size)) {
        return (left_size < right_size) ? traverse(left_child, level - 1, target_size) : traverse(right_child, level - 1, target_size);
    } else if (left_size > target) {
        return traverse(left_child, level - 1, target_size);
    } else if (right_size > target_size) {
        return traverse(right_child, level - 1, target_size);
    }

    return -1;
}


void *bd_alloc(uint64 nbytes){

    acquire(&bd.lock);

    unsigned alloc_size = nearest_power_of_2(ROUNDUP(nbytes, LEAF_SIZE));

    if(alloc_size > buddy.largest_possible_block[0]) {
        printf("Not enough memory\n");
        release(&bd.lock);
        return 0;
    }

    printf("Need %d bytes, Allocating %d bytes\n", nbytes, alloc_size);
    int index = traverse(0, levels, alloc_size);

    if (index < 0){
        printf("Error: index < 0\n");
        release(&bd.lock);
        return 0;
    }

    unsigned alloc_mem = buddy.largest_possible_block[index];
    printf("Allocated %d bytes at index %d\n", alloc_mem, index);
    buddy.largest_possible_block[index] = 0; // Mark the block as allocated

    int at_level = get_level(index);



    
}