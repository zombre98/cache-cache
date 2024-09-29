#include <stdio.h>
#include <errno.h>

void die(const char *msg) {
  fprintf(stderr, "[%d] %s\n", errno, msg);
}

void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

