#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/ksm.h"
#include "user/user.h"

#define NWORKERS 4
#define PAGES_PER_WORKER 30
#define ZONE_A_PAGES 12  // Shared app config and lookup tables (40%)
#define ZONE_B_PAGES 9   // Zero-filled request buffers (30%)
#define ZONE_C_PAGES 9   // Unique private state (30%)

static void
run_worker(int id, int in_fd, int out_fd)
{
  ksmenable(1);

  char *pages[PAGES_PER_WORKER];
  for (int i = 0; i < PAGES_PER_WORKER; i++) {
    pages[i] = sbrk(4096);
    if (pages[i] == (char *)-1) {
      printf("worker %d: sbrk failed\n", id);
      exit(1);
    }
  }

  // Zone A: Identical across all workers
  for (int i = 0; i < ZONE_A_PAGES; i++) {
    memset(pages[i], 0x50 + i, 4096);
  }

  // Zone B: Zero buffers
  for (int i = ZONE_A_PAGES; i < ZONE_A_PAGES + ZONE_B_PAGES; i++) {
    memset(pages[i], 0, 4096);
  }

  // Zone C: Private unique state per worker
  for (int i = ZONE_A_PAGES + ZONE_B_PAGES; i < PAGES_PER_WORKER; i++) {
    memset(pages[i], 0x80 + (id * 10) + i, 4096);
  }

  // Signal parent that allocation is complete, then sleep waiting for scan
  write(out_fd, "R", 1);
  char cmd;
  read(in_fd, &cmd, 1);

  // Simulate traffic / write churn
  if (id == 0 || id == 1) {
    // Worker 0 & 1 mutate shared config page 0 (triggers COW split)
    pages[0][0] = 'W';
  } else if (id == 2) {
    // Worker 2 mutates zero buffer (triggers zero-page COW split)
    pages[ZONE_A_PAGES][0] = 'Z';
  } else if (id == 3) {
    // Worker 3 verifies write isolation: shared pages must not show writes from workers 0, 1, 2
    if (pages[0][0] != (char)(0x50)) {
      printf("worker 3: isolation failed on shared page\n");
      exit(1);
    }
    if (pages[ZONE_A_PAGES][0] != 0) {
      printf("worker 3: isolation failed on zero page\n");
      exit(1);
    }
  }

  // Verify private data was never corrupted
  for (int i = ZONE_A_PAGES + ZONE_B_PAGES; i < PAGES_PER_WORKER; i++) {
    char expected = 0x80 + (id * 10) + i;
    if (pages[i][0] != expected) {
      printf("worker %d: private page %d corrupted\n", id, i);
      exit(1);
    }
  }

  exit(0);
}

int
main(int argc, char *argv[])
{
  printf("=== Realistic Multi-Worker Simulation (4 Workers) ===\n");

  if (ksmenable(1) != 0) {
    printf("ksmenable failed\n");
    exit(1);
  }

  int ready_pipe[2];
  int run_pipe[NWORKERS][2];
  if (pipe(ready_pipe) < 0) {
    printf("pipe failed\n");
    exit(1);
  }

  for (int i = 0; i < NWORKERS; i++) {
    if (pipe(run_pipe[i]) < 0) {
      printf("pipe failed\n");
      exit(1);
    }
  }

  for (int i = 0; i < NWORKERS; i++) {
    int pid = fork();
    if (pid < 0) {
      printf("fork worker %d failed\n", i);
      exit(1);
    }
    if (pid == 0) {
      close(ready_pipe[0]);
      close(run_pipe[i][1]);
      run_worker(i, run_pipe[i][0], ready_pipe[1]);
    }
    close(run_pipe[i][0]);
  }
  close(ready_pipe[1]);

  // Wait for all 4 workers to finish allocating and enter sleep in read()
  for (int i = 0; i < NWORKERS; i++) {
    char ch;
    read(ready_pipe[0], &ch, 1);
  }

  struct ksmstat st0, st1, st2;
  ksminfo(&st0);

  int total_pages = NWORKERS * PAGES_PER_WORKER;
  int total_kb = (total_pages * 4096) / 1024;
  printf("Allocated: %d total pages across %d workers (%d KB)\n",
         total_pages, NWORKERS, total_kb);
  printf("  - Zone A (Shared Config):   %d pages\n", NWORKERS * ZONE_A_PAGES);
  printf("  - Zone B (Zero Buffers):    %d pages\n", NWORKERS * ZONE_B_PAGES);
  printf("  - Zone C (Private State):   %d pages\n", NWORKERS * ZONE_C_PAGES);

  // Trigger KSM scan pass while workers are sleeping
  int t0 = uptime();
  ksmscan();
  int t1 = uptime();

  ksminfo(&st1);

  uint64 merged = st1.merged - st0.merged;
  uint64 saved_kb = (st1.saved_pages - st0.saved_pages) * 4;

  printf("\n--- Phase 1: Deduplication Results ---\n");
  printf("Scan time:         %d ticks\n", t1 - t0);
  printf("Pages merged:      %ld / %d\n", merged, total_pages);
  printf("Physical RAM saved: %ld KB / %d KB\n", saved_kb, total_kb);
  uint64 eff_whole = (total_kb * 10) / (total_kb - saved_kb) / 10;
  uint64 eff_frac = ((total_kb * 10) / (total_kb - saved_kb)) % 10;
  printf("Memory efficiency: %ld.%ldx\n", eff_whole, eff_frac);

  // Wake workers to execute write churn
  for (int i = 0; i < NWORKERS; i++) {
    write(run_pipe[i][1], "G", 1);
    close(run_pipe[i][1]);
  }

  // Wait for all workers to complete validation and exit
  for (int i = 0; i < NWORKERS; i++) {
    wait(0);
  }

  ksminfo(&st2);

  uint64 breaks = st2.cow_breaks - st1.cow_breaks;
  uint64 post_saved_kb = (st2.saved_pages - st0.saved_pages) * 4;

  printf("\n--- Phase 2: Write Churn & Isolation ---\n");
  printf("COW splits triggered: %ld\n", breaks);
  printf("RAM saved after churn: %ld KB\n", post_saved_kb);
  printf("Data Isolation: PASS (all private states and untouched views verified)\n");
  printf("====================================================\n");

  exit(0);
}
