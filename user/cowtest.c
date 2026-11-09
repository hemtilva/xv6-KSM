#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Test 1: Simple COW write isolation
void
simple_test()
{
  printf("simple_test: ");
  char *p = sbrk(4096);
  p[0] = 'X';

  int pid = fork();
  if (pid < 0) {
    printf("fork failed\n");
    exit(1);
  }

  if (pid == 0) {
    if (p[0] != 'X') {
      printf("child saw wrong data\n");
      exit(1);
    }
    p[0] = 'Y'; // Triggers COW fault
    if (p[0] != 'Y') {
      printf("child failed write\n");
      exit(1);
    }
    exit(0);
  }

  wait(0);
  if (p[0] != 'X') {
    printf("parent memory was corrupted!\n");
    exit(1);
  }
  printf("OK\n");
}

// Test 2: Multiple children modifying the same shared page
void
multi_child_test()
{
  printf("multi_child_test: ");
  char *p = sbrk(4096);
  p[0] = 'A';

  int p1 = fork();
  if (p1 == 0) {
    p[0] = 'B';
    exit(0);
  }

  int p2 = fork();
  if (p2 == 0) {
    p[0] = 'C';
    exit(0);
  }

  wait(0);
  wait(0);
  if (p[0] != 'A') {
    printf("parent memory was corrupted by children!\n");
    exit(1);
  }
  printf("OK\n");
}

int
main()
{
  printf("cowtest starting\n");
  simple_test();
  multi_child_test();
  printf("ALL COW TESTS PASSED\n");
  exit(0);
}
