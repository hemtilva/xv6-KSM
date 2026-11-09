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
#define KSM_MAX_PROBES 16

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

__attribute__((unused)) static uint64
ksm_lookup_or_insert(uint32 hash, uint64 pa)
{
  uint32 idx = hash % KSM_TABLE_SIZE;

  for (int probe = 0; probe < KSM_MAX_PROBES; probe++) {
    struct ksm_slot *slot = &ksm_table[(idx + probe) % KSM_TABLE_SIZE];

    if (slot->pa == 0) {
      slot->hash = hash;
      slot->pa = pa;
      return 0;
    }

    if (slot->pa == pa)
      return 0;

    // Hashes match: do byte-by-byte comparison to eliminate hash collisions.
    if (slot->hash == hash) {
      if (memcmp((void *)slot->pa, (void *)pa, PGSIZE) == 0)
        return slot->pa;
    }
  }

  return 0;
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
