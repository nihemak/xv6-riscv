#include <stdarg.h>
#include <stdbool.h>

#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"

static char digits[] = "0123456789ABCDEF";

static void putc(int fd, char c) { write(fd, &c, 1); }

static void printint(int fd, int xx, int base, bool sgn) {
  char buf[16];
  int i = 0;
  bool neg = false;
  uint x = xx;

  if (sgn && x < 0) {
    neg = true;
    x = -x;
  }

  do {
    buf[i++] = digits[x % base];
  } while ((x /= base) != 0);
  if (neg) buf[i++] = '-';

  while (--i >= 0) putc(fd, buf[i]);
}

static void printptr(int fd, uint64 x) {
  putc(fd, '0');
  putc(fd, 'x');
  for (int i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    putc(fd, digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the given fd. Only understands %d, %x, %p, %s.
void vprintf(int fd, const char *fmt, va_list ap) {
  int state = 0;

  for (int i = 0; fmt[i]; i++) {
    int c = fmt[i] & 0xff;
    if (state == 0) {
      if (c == '%') {
        state = '%';
      } else {
        putc(fd, c);
      }
    } else if (state == '%') {
      char *s;
      switch (c) {
        case 'd':
          printint(fd, va_arg(ap, int), 10, true /* sgn */);
          break;
        case 'l':
          printint(fd, va_arg(ap, uint64), 10, false /* sgn */);
          break;
        case 'x':
          printint(fd, va_arg(ap, int), 16, false /* sgn */);
          break;
        case 'p':
          printptr(fd, va_arg(ap, uint64));
          break;
        case 's':
          s = va_arg(ap, char *);
          if (s == 0) s = "(null)";
          while (*s != 0) {
            putc(fd, *s);
            s++;
          }
          break;
        case 'c':
          putc(fd, va_arg(ap, uint));
          break;
        case '%':
          putc(fd, c);
          break;
        default:
          // Unknown % sequence.  Print it to draw attention.
          putc(fd, '%');
          putc(fd, c);
          break;
      }
      state = 0;
    }
  }
}

void fprintf(int fd, const char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  vprintf(fd, fmt, ap);
}

void printf(const char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  vprintf(1, fmt, ap);
}
