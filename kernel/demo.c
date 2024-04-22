#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "syscall.h"

uint64
sys_demo(void)
{
  printf("Hello from sys_demo\n");
  // char* p1 = buddy_alloc(2*1024); // 2097152
  // printf("p1: %p\n", p1);
  // char* p2 = buddy_alloc(3*1024);
  // char* p3 = buddy_alloc(510);
  // buddy_free(p2);
  // buddy_free(p1);
  // buddy_alloc(5*1024*1024);
  // char* p4 = buddy_alloc(32);
  //   buddy_free(p1);
  //   buddy_free(p3);
  //   buddy_free(p4);


  // test buddy_alloc
  char *p1 = buddy_alloc(100);
  printf("p1: %p\n", p1);
  char *p2 = buddy_alloc(900);
  printf("p2: %p\n", p2);
  char *p3 = buddy_alloc(1000);
  printf("p3: %p\n", p3);
  char *p4 = buddy_alloc(1000);
  printf("p4: %p\n", p4);

  buddy_free(p1);
  buddy_free(p2);
  buddy_free(p3);
  buddy_free(p4);

  // printf("alloc 1\n\n");
  // char* p1 = buddy_alloc(8);
  // printf("free 1\n\n");
  // buddy_free(p1);

  // printf("alloc 2\n\n");
  // char* p2 = buddy_alloc(16);
  
  // printf("alloc 3\n\n");
  // char* p3 = buddy_alloc(16);

  // printf("alloc 4\n\n");
  // char* p4 = buddy_alloc(16);

  // printf("free 2\n\n");
  // buddy_free(p2);

  // printf("free 3\n\n");
  // buddy_free(p3);

  // printf("free 4\n\n");
  // buddy_free(p4);

  // printf("alloc 5\n\n");
  // p3 = buddy_alloc(32);
  // buddy_free(p3);
  // p4 = buddy_alloc(64);
  // buddy_free(p4);
  // char* p5 = buddy_alloc(128);
  // buddy_free(p5);
  // char* p6 = buddy_alloc(256);
  // buddy_free(p6);
  // char* p7 = buddy_alloc(128*16);
  // buddy_free(p7);
  // char* p8 = buddy_alloc(512*16);
  // buddy_free(p8);
  // char* p9 = buddy_alloc(1024*16);
  // buddy_free(p9);
  // char* p10 = buddy_alloc(2048*16);
  // buddy_free(p10);
  

    return 0;
}