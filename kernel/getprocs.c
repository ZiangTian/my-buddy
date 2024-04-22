#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "syscall.h"

extern int get_procs(void);

uint64
sys_getprocs(void)
{
  return get_procs();
}
