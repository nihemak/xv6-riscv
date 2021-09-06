// Simple grep.  Only supports ^ . * $ operators.

#include <stdbool.h>

#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"

bool match(char *, char *);

void grep(char *pattern, int fd) {
  int n, m = 0;
  char buf[1024];

  while ((n = read(fd, buf + m, sizeof(buf) - m - 1)) > 0) {
    char *p, *q;
    m += n;
    buf[m] = '\0';
    p = buf;
    while ((q = strchr(p, '\n')) != 0) {
      *q = 0;
      if (match(pattern, p)) {
        *q = '\n';
        write(1, p, q + 1 - p);
      }
      p = q + 1;
    }
    if (m > 0) {
      m -= p - buf;
      memmove(buf, p, m);
    }
  }
}

int main(int argc, char *argv[]) {
  char *pattern;

  if (argc <= 1) {
    fprintf(2, "usage: grep pattern [file ...]\n");
    exit(1);
  }
  pattern = argv[1];

  if (argc <= 2) {
    grep(pattern, 0);
    exit(0);
  }

  for (int i = 2; i < argc; i++) {
    int fd = open(argv[i], 0);
    if (fd < 0) {
      printf("grep: cannot open %s\n", argv[i]);
      exit(1);
    }
    grep(pattern, fd);
    close(fd);
  }
  exit(0);
}

// Regexp matcher from Kernighan & Pike,
// The Practice of Programming, Chapter 9.

bool matchhere(char *, char *);
bool matchstar(int, char *, char *);

bool match(char *regexp, char *text) {
  if (regexp[0] == '^') return matchhere(regexp + 1, text);
  do {  // must look at empty string
    if (matchhere(regexp, text)) return true;
  } while (*text++ != '\0');
  return false;
}

// matchhere: search for regexp at beginning of text
bool matchhere(char *regexp, char *text) {
  if (regexp[0] == '\0') return true;
  if (regexp[1] == '*') return matchstar(regexp[0], regexp + 2, text);
  if (regexp[0] == '$' && regexp[1] == '\0') return *text == '\0';
  if (*text != '\0' && (regexp[0] == '.' || regexp[0] == *text))
    return matchhere(regexp + 1, text + 1);
  return false;
}

// matchstar: search for c*regexp at beginning of text
bool matchstar(int c, char *regexp, char *text) {
  do {  // a * matches zero or more instances
    if (matchhere(regexp, text)) return true;
  } while (*text != '\0' && (*text++ == c || c == '.'));
  return false;
}
