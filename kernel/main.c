#include <stdbool.h>

#include "defs.h"
#include "memlayout.h"
#include "param.h"
#include "riscv.h"
#include "types.h"

volatile static bool started = false;

void init(void);
void start_hart(int);

// start() jumps here in supervisor mode on all CPUs.
void main() {
  int cpu_id = cpuid();
  if (cpu_id == 0) {
    init();
    __sync_synchronize();
    started = true;
  } else {
    while (!started)
      ;
    __sync_synchronize();
    start_hart(cpu_id);
  }
  scheduler();
}

void init(void) {
  consoleinit();
  printfinit();
  printf(
      "\n"
      "xv6 kernel is booting\n"
      "\n");
  kinit();                          // physical page allocator
  KernelVirtualMemory_init();       // create kernel page table
  KernelVirtualMemory_init_hart();  // turn on paging
  procinit();                       // process table
  trapinit();                       // trap vectors
  trapinithart();                   // install kernel trap vector
  plicinit();                       // set up interrupt controller
  plicinithart();                   // ask PLIC for device interrupts
  binit();                          // buffer cache
  iinit();                          // inode cache
  fileinit();                       // file table
  virtio_disk_init();               // emulated hard disk
  userinit();                       // first user process
}

// Hart: HARdware Thread
void start_hart(int cpu_id) {
  printf("hart %d starting\n", cpu_id);
  KernelVirtualMemory_init_hart();  // turn on paging
  trapinithart();                   // install kernel trap vector
  plicinithart();                   // ask PLIC for device interrupts
}
