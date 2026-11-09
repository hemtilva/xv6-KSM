#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/ksm.h"
#include "user/user.h"

#define NPAGES 100

static void
bench_duplicate_pages(void)
{
  printf("\n--- Benchmark 1: Non-Zero Duplicate Pages ---\n");

  struct ksmstat st0, st1;
  ksminfo(&st0);

  char *pages[NPAGES];
  for (int i = 0; i < NPAGES; i++) {
    pages[i] = sbrk(4096);
    if (pages[i] == (char *)-1) {
      printf("sbrk failed\n");
      exit(1);
    }
    // Fill all pages with the same non-zero byte pattern
    memset(pages[i], 0xAA, 4096);
  }

  int t0 = uptime();
  ksmscan();
  int t1 = uptime();

  ksminfo(&st1);

  uint64 pages_merged = st1.merged - st0.merged;
  uint64 bytes_saved = (st1.saved_pages - st0.saved_pages) * 4096;

  printf("Allocated:     %d pages (%d KB)\n", NPAGES, (NPAGES * 4096) / 1024);
  printf("Scan time:     %d ticks\n", t1 - t0);
  printf("Pages merged:  %ld\n", pages_merged);
  printf("Memory saved:  %ld KB\n", bytes_saved / 1024);

  // Trigger COW breaks to verify isolation and unmerge performance
  int tc0 = uptime();
  for (int i = 0; i < NPAGES; i++) {
    pages[i][0] = (char)i;
  }
  int tc1 = uptime();
  printf("COW break time: %d ticks for %d page splits\n", tc1 - tc0, NPAGES);
}

static void
bench_zero_pages(void)
{
  printf("\n--- Benchmark 2: Zero-Page Fast Path ---\n");

  struct ksmstat st0, st1;
  ksminfo(&st0);

  char *pages[NPAGES];
  for (int i = 0; i < NPAGES; i++) {
    pages[i] = sbrk(4096);
    if (pages[i] == (char *)-1) {
      printf("sbrk failed\n");
      exit(1);
    }
    memset(pages[i], 0, 4096);
  }

  int t0 = uptime();
  ksmscan();
  int t1 = uptime();

  ksminfo(&st1);

  uint64 pages_merged = st1.merged - st0.merged;
  uint64 bytes_saved = (st1.saved_pages - st0.saved_pages) * 4096;

  printf("Allocated:     %d zero pages (%d KB)\n", NPAGES, (NPAGES * 4096) / 1024);
  printf("Scan time:     %d ticks\n", t1 - t0);
  printf("Pages merged:  %ld (all redirected to global zero frame)\n", pages_merged);
  printf("Memory saved:  %ld KB\n", bytes_saved / 1024);
}

int
main(int argc, char *argv[])
{
  printf("=== Mini-KSM Deduplication Benchmark ===\n");

  if (ksmenable(1) != 0) {
    printf("ksmenable failed\n");
    exit(1);
  }

  bench_duplicate_pages();
  bench_zero_pages();

  struct ksmstat st;
  ksminfo(&st);
  printf("\n--- Cumulative KSM Totals ---\n");
  printf("Total scanned:    %ld pages\n", st.scanned);
  printf("Total merged:     %ld pages\n", st.merged);
  printf("Net saved now:    %ld pages (%ld KB)\n", st.saved_pages, (st.saved_pages * 4096) / 1024);
  printf("Total COW breaks: %ld\n", st.cow_breaks);
  printf("========================================\n");

  exit(0);
}
