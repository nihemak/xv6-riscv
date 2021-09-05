#ifndef __KERNEL__SLEEPLOCK_H__
#define __KERNEL__SLEEPLOCK_H__

#include "kernel/spinlock.h"
#include "kernel/types.h"

// Long-term locks for processes
struct sleeplock {
  uint locked;         // Is the lock held?
  struct spinlock lk;  // spinlock protecting this sleep lock

  // For debugging:
  char *name;  // Name of lock.
  int pid;     // Process holding lock
};

#endif
