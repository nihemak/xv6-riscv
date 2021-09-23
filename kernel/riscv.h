#ifndef __KERNEL__RISCV_H__
#define __KERNEL__RISCV_H__

#include "kernel/types.h"

#define READ_CSRR(register)                       \
  {                                               \
    uint64 x;                                     \
    asm volatile("csrr %0, " register : "=r"(x)); \
    return x;                                     \
  }

#define WRITE_CSRW(register, value) \
  asm volatile("csrw " register ", %0" : : "r"(value));

#define READ_MV(register)                       \
  {                                             \
    uint64 x;                                   \
    asm volatile("mv %0, " register : "=r"(x)); \
    return x;                                   \
  }

#define WRITE_MV(register, value) \
  asm volatile("mv " register ", %0" : : "r"(value));

// which hart (core) is this?
static inline uint64 read_mhartid() { READ_CSRR("mhartid") }

// Machine Status Register, mstatus
#define MSTATUS_MPP_MASK (3L << 11)  // previous mode.
#define MSTATUS_MPP_M (3L << 11)
#define MSTATUS_MPP_S (1L << 11)
#define MSTATUS_MPP_U (0L << 11)
#define MSTATUS_MIE (1L << 3)  // machine-mode interrupt enable.
static inline uint64 read_mstatus() { READ_CSRR("mstatus") }
static inline void write_mstatus(uint64 x) { WRITE_CSRW("mstatus", x) }

// machine exception program counter, holds the
// instruction address to which a return from
// exception will go.
static inline void write_mepc(uint64 x) { WRITE_CSRW("mepc", x) }

// Supervisor Status Register, sstatus
#define SSTATUS_SPP (1L << 8)   // Previous mode, 1=Supervisor, 0=User
#define SSTATUS_SPIE (1L << 5)  // Supervisor Previous Interrupt Enable
#define SSTATUS_UPIE (1L << 4)  // User Previous Interrupt Enable
#define SSTATUS_SIE (1L << 1)   // Supervisor Interrupt Enable
#define SSTATUS_UIE (1L << 0)   // User Interrupt Enable
static inline uint64 read_sstatus() { READ_CSRR("sstatus") }
static inline void write_sstatus(uint64 x) { WRITE_CSRW("sstatus", x) }

// Supervisor Interrupt Pending
static inline uint64 read_sip() { READ_CSRR("sip") }
static inline void write_sip(uint64 x) { WRITE_CSRW("sip", x) }

// Supervisor Interrupt Enable
#define SIE_SEIE (1L << 9)  // external
#define SIE_STIE (1L << 5)  // timer
#define SIE_SSIE (1L << 1)  // software
static inline uint64 read_sie() { READ_CSRR("sie") }
static inline void write_sie(uint64 x) { WRITE_CSRW("sie", x) }

// Machine-mode Interrupt Enable
#define MIE_MEIE (1L << 11)  // external
#define MIE_MTIE (1L << 7)   // timer
#define MIE_MSIE (1L << 3)   // software
static inline uint64 read_mie() { READ_CSRR("mie") }
static inline void write_mie(uint64 x) { WRITE_CSRW("mie", x) }

// machine exception program counter, holds the
// instruction address to which a return from
// exception will go.
static inline void write_sepc(uint64 x) { WRITE_CSRW("sepc", x) }
static inline uint64 read_sepc() { READ_CSRR("sepc") }

// Machine Exception Delegation
static inline uint64 read_medeleg() { READ_CSRR("medeleg") }
static inline void write_medeleg(uint64 x) { WRITE_CSRW("medeleg", x) }

// Machine Interrupt Delegation
static inline uint64 read_mideleg() { READ_CSRR("mideleg") }
static inline void write_mideleg(uint64 x) { WRITE_CSRW("mideleg", x) }

// Supervisor Trap-Vector Base Address
// low two bits are mode.
static inline void write_stvec(uint64 x) { WRITE_CSRW("stvec", x) }
static inline uint64 read_stvec() { READ_CSRR("stvec") }

// Machine-mode interrupt vector
static inline void write_mtvec(uint64 x) { WRITE_CSRW("mtvec", x) }

// use riscv's sv39 page table scheme.
#define SATP_SV39 (8L << 60)

#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64)pagetable) >> 12))

// supervisor address translation and protection;
// holds the address of the page table.
static inline void write_satp(uint64 x) { WRITE_CSRW("satp", x) }
static inline uint64 read_satp() { READ_CSRR("satp") }

// Supervisor Scratch register, for early trap handler in trampoline.S.
static inline void write_sscratch(uint64 x) { WRITE_CSRW("sscratch", x) }

static inline void write_mscratch(uint64 x) { WRITE_CSRW("mscratch", x) }

// Supervisor Trap Cause
static inline uint64 read_scause() { READ_CSRR("scause") }

// Supervisor Trap Value
static inline uint64 read_stval() { READ_CSRR("stval") }

// Machine-mode Counter-Enable
static inline void write_mcounteren(uint64 x) { WRITE_CSRW("mcounteren", x) }
static inline uint64 read_mcounteren() { READ_CSRR("mcounteren") }

// machine-mode cycle counter
static inline uint64 read_time() { READ_CSRR("time") }

// enable device interrupts
static inline void intr_on() { write_sstatus(read_sstatus() | SSTATUS_SIE); }

// disable device interrupts
static inline void intr_off() { write_sstatus(read_sstatus() & ~SSTATUS_SIE); }

// are device interrupts enabled?
static inline int intr_get() {
  uint64 x = read_sstatus();
  return (x & SSTATUS_SIE) != 0;
}

static inline uint64 read_sp() { READ_MV("sp") }

// read and write tp, the thread pointer, which holds
// this core's hartid (core number), the index into cpus[].
static inline uint64 read_tp() { READ_MV("tp") }
static inline void write_tp(uint64 x) { WRITE_MV("tp", x) }

static inline uint64 read_ra() { READ_MV("ra") }

// flush the TLB.
static inline void sfence_vma() {
  // the zero, zero means flush all TLB entries.
  asm volatile("sfence.vma zero, zero");
}

#define PGSIZE 4096  // bytes per page
#define PGSHIFT 12   // bits of offset within a page

#define PGROUNDUP(sz) (((sz) + PGSIZE - 1) & ~(PGSIZE - 1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE - 1))

#define PTE_V (1L << 0)  // valid
#define PTE_R (1L << 1)
#define PTE_W (1L << 2)
#define PTE_X (1L << 3)
#define PTE_U (1L << 4)  // 1 -> user can access

// shift a physical address to the right place for a PTE.
#define PA2PTE(pa) ((((uint64)pa) >> 12) << 10)

#define PTE2PA(pte) (((pte) >> 10) << 12)

#define PTE_FLAGS(pte) ((pte)&0x3FF)

// extract the three 9-bit page table indices from a virtual address.
#define PXMASK 0x1FF  // 9 bits
#define PXSHIFT(level) (PGSHIFT + (9 * (level)))
#define PX(level, va) ((((uint64)(va)) >> PXSHIFT(level)) & PXMASK)

// one beyond the highest possible virtual address.
// MAXVA is actually one bit less than the max allowed by
// Sv39, to avoid having to sign-extend virtual addresses
// that have the high bit set.
#define MAX_VIRTUAL_ADDRESS (1L << (9 + 9 + 9 + 12 - 1))

typedef uint64 pte_t;
typedef uint64 *pagetable_t;  // 512 PTEs

#endif
