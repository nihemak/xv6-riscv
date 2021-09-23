#include "defs.h"
#include "memlayout.h"
#include "param.h"
#include "riscv.h"
#include "types.h"

void main();
void timerinit();

// entry.S needs one stack per CPU.
__attribute__((aligned(16))) char stack0[4096 * CPU_MAX_NUM];

// a scratch area per CPU for machine-mode timer interrupts.
uint64 timer_scratch[CPU_MAX_NUM][5];

// assembly code in kernelvec.S for machine-mode timer interrupt.
extern void timervec();

// entry.S jumps here in machine mode on stack0.
void start() {
  // set M Previous Privilege mode to Supervisor, for mret.
  unsigned long x = read_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  write_mstatus(x);

  // set M Exception Program Counter to main, for mret.
  // requires gcc -mcmodel=medany
  write_mepc((uint64)main);

  // disable paging for now.
  write_satp(0);

  // delegate all interrupts and exceptions to supervisor mode.
  write_medeleg(0xffff);
  write_mideleg(0xffff);
  write_sie(read_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  // ask for clock interrupts.
  timerinit();

  // keep each CPU's hartid in its tp register, for cpuid().
  int id = read_mhartid();
  write_tp(id);

  // switch to supervisor mode and jump to main().
  asm volatile("mret");
}

// set up to receive timer interrupts in machine mode,
// which arrive at timervec in kernelvec.S,
// which turns them into software interrupts for
// devintr() in trap.c.
void timerinit() {
  // each CPU has a separate source of timer interrupts.
  int id = read_mhartid();

  // ask the CLINT for a timer interrupt.
  int interval = 1000000;  // cycles; about 1/10th second in qemu.
  *(uint64 *)CLINT_MTIMECMP(id) = *(uint64 *)CLINT_MTIME + interval;

  // prepare information in scratch[] for timervec.
  // scratch[0..2] : space for timervec to save registers.
  // scratch[3] : address of CLINT MTIMECMP register.
  // scratch[4] : desired interval (in cycles) between timer interrupts.
  uint64 *scratch = &timer_scratch[id][0];
  scratch[3] = CLINT_MTIMECMP(id);
  scratch[4] = interval;
  write_mscratch((uint64)scratch);

  // set the machine-mode trap handler.
  write_mtvec((uint64)timervec);

  // enable machine-mode interrupts.
  write_mstatus(read_mstatus() | MSTATUS_MIE);

  // enable machine-mode timer interrupts.
  write_mie(read_mie() | MIE_MTIE);
}
