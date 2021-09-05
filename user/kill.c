#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(2, "usage: kill pid...\n");
    exit(1);
  }
  for (int i = 1; i < argc; i++) kill(atoi(argv[i]));
  exit(0);
}
