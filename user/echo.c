#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  for (int i = 1; i < argc; i++) {
    write(1, argv[i], strlen(argv[i]));
    write(1, i + 1 < argc ? " " : "\n", 1);
  }
  exit(0);
}
