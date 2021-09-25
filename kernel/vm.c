#include <stdbool.h>

#include "defs.h"
#include "elf.h"
#include "fs.h"
#include "memlayout.h"
#include "param.h"
#include "riscv.h"
#include "types.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[];  // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t KernalVertualMemory_make(void) {
  pagetable_t kpgtbl = (pagetable_t)kalloc();
  memset(kpgtbl, 0, PAGE_SIZE);

  // uart registers
  KernelVirtualMemory_map(
      kpgtbl, UART0, UART0, PAGE_SIZE,
      PAGE_TABLE_ENTRY_FLAGS_READABLE | PAGE_TABLE_ENTRY_FLAGS_WRITABLE);

  // virtio mmio disk interface
  KernelVirtualMemory_map(
      kpgtbl, VIRTIO0, VIRTIO0, PAGE_SIZE,
      PAGE_TABLE_ENTRY_FLAGS_READABLE | PAGE_TABLE_ENTRY_FLAGS_WRITABLE);

  // PLIC
  KernelVirtualMemory_map(
      kpgtbl, PLIC, PLIC, 0x400000,
      PAGE_TABLE_ENTRY_FLAGS_READABLE | PAGE_TABLE_ENTRY_FLAGS_WRITABLE);

  // map kernel text executable and read-only.
  KernelVirtualMemory_map(
      kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE,
      PAGE_TABLE_ENTRY_FLAGS_READABLE | PAGE_TABLE_ENTRY_FLAGS_EXECUTABLE);

  // map kernel data and the physical RAM we'll make use of.
  KernelVirtualMemory_map(
      kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext,
      PAGE_TABLE_ENTRY_FLAGS_READABLE | PAGE_TABLE_ENTRY_FLAGS_WRITABLE);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  KernelVirtualMemory_map(
      kpgtbl, TRAMPOLINE, (uint64)trampoline, PAGE_SIZE,
      PAGE_TABLE_ENTRY_FLAGS_READABLE | PAGE_TABLE_ENTRY_FLAGS_EXECUTABLE);

  // map kernel stacks
  proc_mapstacks(kpgtbl);

  return kpgtbl;
}

// Initialize the one kernel_pagetable
void KernelVirtualMemory_init(void) {
  kernel_pagetable = KernalVertualMemory_make();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void KernelVirtualMemory_init_hart() {
  write_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *walk(pagetable_t pagetable, uint64 virtual_address, bool alloc) {
  if (virtual_address >= MAX_VIRTUAL_ADDRESS) panic("walk");

  for (int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PAGE_TABLE_INDEX(level, virtual_address)];
    if (*pte & PAGE_TABLE_ENTRY_FLAGS_VALID) {
      pagetable = (pagetable_t)PAGE_TABLE_ENTRY_TO_PHYSICAL_ADDRESS(*pte);
    } else {
      if (!alloc || (pagetable = (pde_t *)kalloc()) == 0) return 0;
      memset(pagetable, 0, PAGE_SIZE);
      *pte = PHYSICAL_ADDRESS_TO_PAGE_TABLE_ENTRY(pagetable) |
             PAGE_TABLE_ENTRY_FLAGS_VALID;
    }
  }
  return &pagetable[PAGE_TABLE_INDEX(0, virtual_address)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64 walkaddr(pagetable_t pagetable, uint64 va) {
  if (va >= MAX_VIRTUAL_ADDRESS) return 0;
  pte_t *pte = walk(pagetable, va, false /* alloc */);
  if (pte == 0) return 0;
  if ((*pte & PAGE_TABLE_ENTRY_FLAGS_VALID) == 0) return 0;
  if ((*pte & PAGE_TABLE_ENTRY_FLAGS_USER) == 0) return 0;
  uint64 pa = PAGE_TABLE_ENTRY_TO_PHYSICAL_ADDRESS(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void KernelVirtualMemory_map(pagetable_t kpgtbl, uint64 virtual_address,
                             uint64 physical_address, uint64 size, int perm) {
  if (!mappages(kpgtbl, virtual_address, size, physical_address, perm))
    panic("KernelVirtualMemmory_map");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
bool mappages(pagetable_t pagetable, uint64 virtual_address, uint64 size,
              uint64 physical_address, int perm) {
  uint64 va = PAGE_ROUND_DOWN(virtual_address);
  uint64 va_last = PAGE_ROUND_DOWN(virtual_address + size - 1);
  uint64 pa = physical_address;

  for (;;) {
    pte_t *pte = walk(pagetable, va, true /* alloc */);
    if (pte == 0) return false;
    if (*pte & PAGE_TABLE_ENTRY_FLAGS_VALID) panic("remap");
    *pte = PHYSICAL_ADDRESS_TO_PAGE_TABLE_ENTRY(pa) | perm |
           PAGE_TABLE_ENTRY_FLAGS_VALID;
    if (va == va_last) break;
    va += PAGE_SIZE;
    pa += PAGE_SIZE;
  }
  return true;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free) {
  if ((va % PAGE_SIZE) != 0) panic("uvmunmap: not aligned");

  for (uint64 a = va; a < va + npages * PAGE_SIZE; a += PAGE_SIZE) {
    pte_t *pte = walk(pagetable, a, false /* alloc */);
    if (pte == 0) panic("uvmunmap: walk");
    if ((*pte & PAGE_TABLE_ENTRY_FLAGS_VALID) == 0)
      panic("uvmunmap: not mapped");
    if (PAGE_TABLE_ENTRY_FLAGS(*pte) == PAGE_TABLE_ENTRY_FLAGS_VALID)
      panic("uvmunmap: not a leaf");
    if (do_free) {
      uint64 pa = PAGE_TABLE_ENTRY_TO_PHYSICAL_ADDRESS(*pte);
      kfree((void *)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t uvmcreate() {
  pagetable_t pagetable = (pagetable_t)kalloc();
  if (pagetable == 0) return 0;
  memset(pagetable, 0, PAGE_SIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void uvminit(pagetable_t pagetable, uchar *src, uint sz) {
  if (sz >= PAGE_SIZE) panic("inituvm: more than a page");
  char *mem = kalloc();
  memset(mem, 0, PAGE_SIZE);
  mappages(pagetable, 0, PAGE_SIZE, (uint64)mem,
           PAGE_TABLE_ENTRY_FLAGS_WRITABLE | PAGE_TABLE_ENTRY_FLAGS_READABLE |
               PAGE_TABLE_ENTRY_FLAGS_EXECUTABLE | PAGE_TABLE_ENTRY_FLAGS_USER);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
  if (newsz < oldsz) return oldsz;

  oldsz = PAGE_ROUND_UP(oldsz);
  for (uint64 a = oldsz; a < newsz; a += PAGE_SIZE) {
    char *mem = kalloc();
    if (mem == 0) {
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PAGE_SIZE);
    if (!mappages(pagetable, a, PAGE_SIZE, (uint64)mem,
                  PAGE_TABLE_ENTRY_FLAGS_WRITABLE |
                      PAGE_TABLE_ENTRY_FLAGS_EXECUTABLE |
                      PAGE_TABLE_ENTRY_FLAGS_READABLE |
                      PAGE_TABLE_ENTRY_FLAGS_USER)) {
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
  if (newsz >= oldsz) return oldsz;

  if (PAGE_ROUND_UP(newsz) < PAGE_ROUND_UP(oldsz)) {
    int npages = (PAGE_ROUND_UP(oldsz) - PAGE_ROUND_UP(newsz)) / PAGE_SIZE;
    uvmunmap(pagetable, PAGE_ROUND_UP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable) {
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PAGE_TABLE_ENTRY_FLAGS_VALID) &&
        (pte &
         (PAGE_TABLE_ENTRY_FLAGS_READABLE | PAGE_TABLE_ENTRY_FLAGS_WRITABLE |
          PAGE_TABLE_ENTRY_FLAGS_EXECUTABLE)) == 0) {
      // this PTE points to a lower-level page table.
      uint64 child = PAGE_TABLE_ENTRY_TO_PHYSICAL_ADDRESS(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if (pte & PAGE_TABLE_ENTRY_FLAGS_VALID) {
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz) {
  if (sz > 0) uvmunmap(pagetable, 0, PAGE_ROUND_UP(sz) / PAGE_SIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
  uint64 i;

  for (i = 0; i < sz; i += PAGE_SIZE) {
    pte_t *pte = walk(old, i, false /* alloc */);
    uint64 pa;
    uint flags;
    char *mem;
    if (pte == 0) panic("uvmcopy: pte should exist");
    if ((*pte & PAGE_TABLE_ENTRY_FLAGS_VALID) == 0)
      panic("uvmcopy: page not present");
    pa = PAGE_TABLE_ENTRY_TO_PHYSICAL_ADDRESS(*pte);
    flags = PAGE_TABLE_ENTRY_FLAGS(*pte);
    if ((mem = kalloc()) == 0) goto err;
    memmove(mem, (char *)pa, PAGE_SIZE);
    if (!mappages(new, i, PAGE_SIZE, (uint64)mem, flags)) {
      kfree(mem);
      goto err;
    }
  }
  return 0;

err:
  uvmunmap(new, 0, i / PAGE_SIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void uvmclear(pagetable_t pagetable, uint64 va) {
  pte_t *pte = walk(pagetable, va, false /* alloc */);
  if (pte == 0) panic("uvmclear");
  *pte &= ~PAGE_TABLE_ENTRY_FLAGS_USER;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
  while (len > 0) {
    uint64 n;
    uint64 va0 = PAGE_ROUND_DOWN(dstva);
    uint64 pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) return -1;
    n = PAGE_SIZE - (dstva - va0);
    if (n > len) n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PAGE_SIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len) {
  while (len > 0) {
    uint64 n;
    uint64 va0 = PAGE_ROUND_DOWN(srcva);
    uint64 pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) return -1;
    n = PAGE_SIZE - (srcva - va0);
    if (n > len) n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PAGE_SIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max) {
  bool got_null = false;
  while (!got_null && max > 0) {
    uint64 n;
    uint64 va0 = PAGE_ROUND_DOWN(srcva);
    uint64 pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) return -1;
    n = PAGE_SIZE - (srcva - va0);
    if (n > max) n = max;

    char *p = (char *)(pa0 + (srcva - va0));
    while (n > 0) {
      if (*p == '\0') {
        *dst = '\0';
        got_null = true;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PAGE_SIZE;
  }
  return got_null ? 0 : -1;
}
