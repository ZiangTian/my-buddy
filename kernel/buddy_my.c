#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

// Buddy system

static int nsizes; // 有多少种块大小。 最大是 nsizes - 1

/*
第一种块大小是2Leaf_size，第二种块大小是4Leaf_size，第三种块大小是8Leaf_size，以此类推
假设一共有6种块大小，那么nsizes=7
第一种块的数量是1<<(7-1-1)=16， 第二种块的数量是1<<(7-1-2)=8, 以此类推，第六种块的数量是1<<(7-1-6)=1
最大块的数量总是1.
*/

#define LEAF_SIZE 16 // 最小的块大小
#define BLK_SIZE(k) ((uint64)1 << (k))*LEAF_SIZE // 第k种块的大小
#define MAX_ENTRY nsizes-1 // 最大的entry数量
#define NBLK(k) (1<<(MAX_ENTRY-k)) // 第k种块的数量
#define ROUNDUP(n,sz) (((((n)-1)/(sz))+1)*(sz))  // round到sz的下一个倍数

typedef struct list list_t;

// size info for each block size
struct size_info {
  list_t free_list; // 空闲块的链表
  char *allocated; // 已经分配的块. 一个bit表示一个块, 因此一个char元素可以表示8个块
  char *split; // 分裂的块  
};

typedef struct size_info size_info_t;

static size_info_t *size_infos; // 每种块的信息 最大元素是 size_infos[MAX_ENTRY]
static void* start; // buddy system的起始地址
// static void* end; // buddy system的结束地址
static struct spinlock lock; // buddy system的锁


// bit operations
int isset(char *array, int index){
    return (array[index/8] >> (index%8)) & 1;
}

void set(char *array, int index){
    array[index/8] |= (1 << (index%8));
}

void unset(char *array, int index){
    array[index/8] &= ~(1 << (index%8));
}

int find_a_size(uint64 n){
    int k = 0;
    uint64 size = LEAF_SIZE;

    while(size < n){
        size *= 2;
        k++;
    }

    return k;
}


int get_block_index_from_addr(int k, char *p){
    int offset = p - (char *)start;
    return offset / BLK_SIZE(k);
}

void* get_addr_from_block_index(int k, int i){
    return (char *)start + i * BLK_SIZE(k);
}


void* bd_malloc(uint64 num_bytes){
    acquire(&lock);
    // 底层都是blocks，buddy只是提供了不同的视图管理这些blocks

    // find a proper block size
    int first_fit = find_a_size(num_bytes);
    int k;

    // from first_fit to the largest block size, 
    // find the first non-empty list
    for(k = first_fit; k < nsizes; k++){
        if(!lst_empty(&size_infos[k].free_list)){
            break;
        }
    }
    if(k >= nsizes) {
        release(&lock);
        return 0;
    }

    // found a non-empty list, pop the first block
    char *p = (char *)lst_pop(&size_infos[k].free_list);
    // mark the block in size k as allocated
    set(size_infos[k].allocated, get_block_index_from_addr(k, p));


    for(; k > first_fit; k--){
        // split the block into two blocks
        char *buddy = p + BLK_SIZE(k-1);
        // mark the block as split
        set(size_infos[k].split, get_block_index_from_addr(k, p));  
        // mark the original block as allocated
        set(size_infos[k-1].allocated, get_block_index_from_addr(k-1, p));
        // mark the buddy block as free
        lst_push(&size_infos[k-1].free_list, buddy);
    }

    release(&lock);

    return p;
}

// get the block size type from the address
int get_block_size(char *p){

    // we check from the smallest block size
    // if the higher level block is split, this means the current block size is being used
    for(int k = 0; k < nsizes; k++){
        if(isset(size_infos[k+1].split, get_block_index_from_addr(k+1, p))){
            return k;
        }
    }
    return 0;
}

// free memory pointed to by p, and combine the buddy blocks if possible
void bd_free(void *p){
    
    acquire(&lock);
    int k;
    for(k = get_block_size(p); k < MAX_ENTRY; k++){
        // get block index at level k
        int blk_id = get_block_index_from_addr(k, p);
        // find the buddy block. 
        // If the block is at even index, the buddy block is at blk_id + 1, 
        // otherwise, the buddy block is at blk_id - 1

        // free the block
        int buddy_id = (blk_id % 2 == 0) ? blk_id + 1 : blk_id - 1;
        unset(size_infos[k].allocated, blk_id);

        // check if buddy is allocated or split !!!!!!!
        if(isset(size_infos[k].allocated, buddy_id) || isset(size_infos[k].split, buddy_id)){
            break; // if allocated, can't combine. break out.
        }

        // buddy is free
        void* q = get_addr_from_block_index(k, buddy_id);
        lst_remove((list_t *)q); // remove the buddy block from the free list

        // combine ptrs
        p = (buddy_id % 2 == 0) ? q : p;

        // at level k, the block is combined, so at level k+1 it is not split
        unset(size_infos[k+1].split, get_block_index_from_addr(k+1, p));
    }

    // add the combined block to the free list
    lst_push(&size_infos[k].free_list, p);
    release(&lock);
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

int get_following_block(int k, char *meta_data_end){
    int n = (meta_data_end - (char *)start) / BLK_SIZE(k);
    if((meta_data_end - (char *)start) % BLK_SIZE(k) != 0){
        n++;
    }
    return n;
}

// if a block is allocated but its buddy is free, put the buddy on the free list
// return the size of the block
int check_and_add_buddy(int k, int i){
    int buddy_id = (i % 2 == 0) ? i + 1 : i - 1, free = 0;
    printf("check_and_add_buddy: %d\n", isset(size_infos[k].allocated, i));
    // if one of them is allocated and the other is free, add the free one to the free list
    if(isset(size_infos[k].allocated, i) != isset(size_infos[k].allocated, buddy_id)){
        free = BLK_SIZE(k);
        // if buddy is free, add it to the free list
        if(isset(size_infos[k].allocated, i)){
            lst_push(&size_infos[k].free_list, get_addr_from_block_index(k, buddy_id));
        } else {
            lst_push(&size_infos[k].free_list, get_addr_from_block_index(k, i));
        }
    }
    return free;
}

void mark_as_allocated(void* head, void* tail){
    int bi, bj;
    if(((uint64) head % LEAF_SIZE != 0) || ((uint64) tail % LEAF_SIZE != 0) ){
        panic("mark_as_allocated: not aligned");
    }

    for (int k = 0; k < nsizes; k++)
    {
        bi = get_block_index_from_addr(k, head);
        bj = get_following_block(k, tail);

        for(; bi < bj; bi++){
            set(size_infos[k].split, bi);
            set(size_infos[k].allocated, bi);
        }
    }
    
}

int
bd_initfree(void *bd_left, void *bd_right) {
  int free = 0;
  printf("bd_initfree: bd_left: %p, bd_right: %p\n", bd_left, bd_right);
  for (int k = 0; k < MAX_ENTRY; k++) {   // skip max size
    int left = get_following_block(k, bd_left);
    printf("bd_initfree: k: %d, left: %d\n", k, left);
    int right = get_block_index_from_addr(k, bd_right);
    printf("bd_initfree: k: %d, right: %d\n", k, right);
    free += check_and_add_buddy(k, left);
    printf("bd_initfree: free: %d\n", free);
    if(right <= left)
      continue;
    free += check_and_add_buddy(k, right);
  }
  return free;
}


void bd_init(void* base, void* end){
    // Round up base
    printf("bd_init: base: %p, end: %p\n", base, end);
    char* base_start = (char *)ROUNDUP((uint64)base, LEAF_SIZE);

    initlock(&lock, "buddy");
    start = (void*) base_start;

    // calculate the number of block sizes
    nsizes = log2(((char*)end - base_start)/LEAF_SIZE) + 1; // 例如，如果有1024*16个字节，那么应该有2^0~2^10一共11种块
    if((char*)end - base_start > BLK_SIZE(MAX_ENTRY)){ // 例如，如果有1025*16个字节，那么应该有12种块，但是最上面一层有一半是空的
        nsizes++;
    }

    size_infos = (size_info_t *)base_start;
    char *p = base_start + sizeof(size_info_t) * nsizes;
    memset(size_infos, 0, sizeof(size_info_t) * nsizes);

    // init free and allocated lists
    for(int k = 0; k < nsizes; k++){
        lst_init(&size_infos[k].free_list);
        // allocate the alloc array for size k
        size_infos[k].allocated = p;
        // calculate the size of the alloc array
        int alloc_size = sizeof(char) * (ROUNDUP(NBLK(k), 8))/8;
        memset(size_infos[k].allocated, 0, alloc_size);
        p += alloc_size;
    }

    // init the split array
    for(int k = 0; k < nsizes; k++){
        size_infos[k].split = p;
        int split_size = sizeof(char) * (ROUNDUP(NBLK(k), 8))/8;
        memset(size_infos[k].split, 0, split_size);
        p += split_size;
    }

    // p is at the end of the meta data
    p = (char *)ROUNDUP((uint64)p, LEAF_SIZE);

    // mark our management meta data as allocated
    int meta_data_size = (char*)p - base_start;
    printf("Meta data inited. meta_data_size: %d\n", meta_data_size);
    
    mark_as_allocated(base_start, p);
    printf("Marked meta data as allocated\n");

    // 我们还多虚分配了好多内存，nsizes实际偏大，需要把它们设为不可分配
    int non_exist_size = BLK(MAX_ENTRY) - (end-start);
    if(non_exist_size > 0){
        non_exist_size = ROUNDUP(non_exist_size, LEAF_SIZE);
        printf("Non exist size: %d\n", non_exist_size);
        void *bd_end = base_start + BLK_SIZE(MAX_ENTRY) - non_exist_size;
        mark_as_allocated(bd_end, base_start + BLK_SIZE(MAX_ENTRY));
    }
    else{
        printf("No non exist size\n");
        void* bd_end = base_start + BLK_SIZE(MAX_ENTRY);
    }
    // init free lists for each block size
    int free_size = 0;
    
    // for(int k = 0; k < MAX_ENTRY; k++){
    //     int left = get_following_block(k, p);
    //     int right = get_block_index_from_addr(k, bd_end);

    //     free_size += check_and_add_buddy(k, left);
    //     if(right <= left) continue;
    //     free_size += check_and_add_buddy(k, right);
    // }
    free_size = bd_initfree(p, bd_end);
    printf("Free size: %d\n", free_size);

    if (free_size != BLK_SIZE(MAX_ENTRY) - meta_data_size){
        panic("buddy system init error");
    }

}