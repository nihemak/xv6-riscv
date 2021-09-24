// init: The initial user-level program

#include "kernel/fcntl.h"
#include "kernel/file.h"
#include "kernel/fs.h"
#include "kernel/sleeplock.h"
#include "kernel/spinlock.h"
#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"

char *argv[] = {"sh", 0};

void init_stdout_stderr(void);

int main(void) {
  init_stdout_stderr();

  for (;;) {
    printf("init: starting sh\n");
    int shell_pid = fork();
    if (shell_pid < 0) {
      printf("init: fork failed\n");
      exit(1);
    }
    if (shell_pid == 0) {  // child is shell
      exec("sh", argv);
      printf("init: exec sh failed\n");
      exit(1);
    }

    for (;;) {
      // this call to wait() returns if the shell exits,
      // or if a parentless process exits.
      int wait_pid = wait((int *)0);
      if (wait_pid == shell_pid) break;  // the shell exited; restart it.
      if (wait_pid < 0) {
        printf("init: wait returned an error\n");
        exit(1);
      }
      // it was a parentless process; do nothing.
    }
  }
}

void init_stdout_stderr(void) {
  if (open("console", O_RDWR) < 0) {
    mknod("console", CONSOLE, 0);
    open("console", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr
}
