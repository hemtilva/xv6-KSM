#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/ksm.h"
#include "user/user.h"

// Test 1: Deduplicating identical pages within a single process
void
test_intra_process_merge(void)
{
  printf("test_intra_process_merge: ");

  struct ksmstat st_before, st_after;
  ksminfo(&st_before);

  // Allocate 2 distinct 4KB pages
  char *p1 = sbrk(4096);
  char *p2 = sbrk(4096);

  // Fill both pages with the identical pattern
  memset(p1, 'M', 4096);
  memset(p2, 'M', 4096);

  // Run the KSM scanner
  ksmscan();

  ksminfo(&st_after);
  if (st_after.merged <= st_before.merged) {
    printf("FAILED (pages not merged: before=%ld, after=%ld)\n",
           st_before.merged, st_after.merged);
    exit(1);
  }

  // Verify write isolation: writing to p1 must not affect p2
  p1[0] = 'Z';
  if (p2[0] != 'M') {
    printf("FAILED (write isolation broken: p2[0] was overwritten)\n");
    exit(1);
  }

  printf("OK\n");
}

// Test 2: Deduplicating identical pages across two independent processes
void
test_inter_process_merge(void)
{
  printf("test_inter_process_merge: ");

  // Two pipes for deterministic bidirectional handshake: c2p and p2c
  int c2p[2], p2c[2];
  if (pipe(c2p) < 0 || pipe(p2c) < 0) {
    printf("pipe failed\n");
    exit(1);
  }

  int pid = fork();
  if (pid < 0) {
    printf("fork failed\n");
    exit(1);
  }

  if (pid == 0) {
    close(c2p[0]);
    close(p2c[1]);

    ksmenable(1);
    char *p = sbrk(4096);
    memset(p, 'K', 4096);

    // Notify parent that child page is allocated and ready
    write(c2p[1], "go", 2);

    // Block in sleep until parent finishes ksmscan
    char ack[2];
    read(p2c[0], ack, 2);

    // Verify child data is intact
    if (p[0] != 'K' || p[4095] != 'K') {
      printf("child data corrupted\n");
      exit(1);
    }

    // Trigger COW break
    p[0] = 'Q';
    if (p[0] != 'Q') {
      printf("child write failed\n");
      exit(1);
    }

    close(c2p[1]);
    close(p2c[0]);
    exit(0);
  }

  close(c2p[1]);
  close(p2c[0]);

  char *p = sbrk(4096);
  memset(p, 'K', 4096);

  // Wait until child is sleeping in read()
  char sync_buf[2];
  read(c2p[0], sync_buf, 2);

  struct ksmstat st_before, st_after;
  ksminfo(&st_before);

  ksmscan();

  ksminfo(&st_after);
  if (st_after.merged <= st_before.merged) {
    printf("FAILED (cross-process merge did not happen)\n");
    exit(1);
  }

  // Wake up child
  write(p2c[1], "ok", 2);

  wait(0);

  if (p[0] != 'K') {
    printf("FAILED (parent corrupted by child write)\n");
    exit(1);
  }

  close(c2p[0]);
  close(p2c[1]);
  printf("OK\n");
}

// Test 3: Deduplicating all-zero pages into the dedicated zero page frame
void
test_zero_page_merge(void)
{
  printf("test_zero_page_merge: ");

  struct ksmstat st_before, st_after;
  ksminfo(&st_before);

  char *zp = sbrk(4 * 4096);
  memset(zp, 0, 4 * 4096);

  ksmscan();

  ksminfo(&st_after);
  if (st_after.merged < st_before.merged + 4) {
    printf("FAILED (zero pages not merged: before=%ld, after=%ld)\n",
           st_before.merged, st_after.merged);
    exit(1);
  }

  // Trigger COW breaks and verify data isolation
  zp[0] = 'X';
  zp[4096] = 'Y';
  if (zp[0] != 'X' || zp[4096] != 'Y' || zp[4096 * 2] != 0) {
    printf("FAILED (zero page COW isolation broken)\n");
    exit(1);
  }

  printf("OK\n");
}

int
main(void)
{
  printf("ksmtest starting\n");

  if (ksmenable(1) != 0) {
    printf("ksmenable failed\n");
    exit(1);
  }

  test_intra_process_merge();
  test_inter_process_merge();
  test_zero_page_merge();

  struct ksmstat st;
  ksminfo(&st);
  printf("KSM Stats: scanned=%ld, merged=%ld, saved=%ld, cow_breaks=%ld\n",
         st.scanned, st.merged, st.saved_pages, st.cow_breaks);

  printf("ALL KSM TESTS PASSED\n");
  exit(0);
}
