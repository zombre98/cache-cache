#include <stdio.h>
#include <errno.h>

#ifndef LOGGING_H
#define LOGGING_H

static void die(const char *msg) {
  fprintf(stderr, "[%d] %s\n", errno, msg);
}

static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

#endif // !LOGGING_H
