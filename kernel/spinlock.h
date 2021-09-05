#ifndef __KERNEL__SPINLOCK_H__
#define __KERNEL__SPINLOCK_H__

#include "kernel/types.h"

// Mutual exclusion lock.
struct spinlock {
  uint locked;  // Is the lock held?

  // For debugging:
  char *name;       // Name of lock.
  struct cpu *cpu;  // The cpu holding the lock.
};

#endif
