#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "ksm.h"

struct {
  struct spinlock lock;
  struct ksmstat stat;
} ksm;

#define KSM_TABLE_SIZE 1024

struct ksm_slot {
  uint32 hash;
  uint64 pa;
};

static struct ksm_slot ksm_table[KSM_TABLE_SIZE];

__attribute__((unused)) static void
ksm_table_reset(void)
{
  memset(ksm_table, 0, sizeof(ksm_table));
}

void
ksminit(void)
{
  initlock(&ksm.lock, "ksm");
  memset(&ksm.stat, 0, sizeof(ksm.stat));
  ksm_table_reset();
}

__attribute__((unused)) static uint32
page_hash(void *page)
{
  uint32 hash = 2166136261u;
  uint64 *p = (uint64 *)page;

  for (int i = 0; i < PGSIZE / sizeof(uint64); i++) {
    uint64 word = p[i];
    hash ^= (uint32)word;
    hash *= 16777619u;
    hash ^= (uint32)(word >> 32);
    hash *= 16777619u;
  }
  return hash;
}

int
ksm_get_stat(struct ksmstat *st)
{
  acquire(&ksm.lock);
  *st = ksm.stat;
  release(&ksm.lock);
  return 0;
}

int
ksm_scan_pass(void)
{
  return 0;
}
