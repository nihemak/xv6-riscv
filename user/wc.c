#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"

void wc(int fd, char *name) {
  int n;
  int l = 0, w = 0, c = 0, inword = 0;
  char buf[512];

  while ((n = read(fd, buf, sizeof(buf))) > 0) {
    for (int i = 0; i < n; i++) {
      c++;
      if (buf[i] == '\n') l++;
      if (strchr(" \r\t\n\v", buf[i]))
        inword = 0;
      else if (!inword) {
        w++;
        inword = 1;
      }
    }
  }
  if (n < 0) {
    printf("wc: read error\n");
    exit(1);
  }
  printf("%d %d %d %s\n", l, w, c, name);
}

int main(int argc, char *argv[]) {
  if (argc <= 1) {
    wc(0, "");
    exit(0);
  }

  for (int i = 1; i < argc; i++) {
    int fd = open(argv[i], 0);
    if (fd < 0) {
      printf("wc: cannot open %s\n", argv[i]);
      exit(1);
    }
    wc(fd, argv[i]);
    close(fd);
  }
  exit(0);
}
