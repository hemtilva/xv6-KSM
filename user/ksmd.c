#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/ksm.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int interval = 50;
  int foreground = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-f") == 0) {
      foreground = 1;
    } else {
      int val = atoi(argv[i]);
      if (val > 0)
        interval = val;
    }
  }

  // Detach by default since xv6 shell lacks SIGINT/job control to interrupt foreground loops.
  if (!foreground) {
    int pid = fork();
    if (pid < 0) {
      printf("ksmd: fork failed\n");
      exit(1);
    }
    if (pid > 0) {
      printf("ksmd started [pid %d, interval %d ticks]\n", pid, interval);
      exit(0);
    }
  } else {
    printf("ksmd running in foreground [interval %d ticks]\n", interval);
  }

  while (1) {
    pause(interval);
    ksmscan();
  }

  exit(0);
}
