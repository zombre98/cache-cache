#include <unistd.h>

#ifndef LOGGING_H
#define LOGGING_H

void die(const char *msg);
void msg(const char *msg);
int32_t read_n(int fd, char *buf, size_t n);
int32_t write_n(int fd, const char *buf, size_t n);

#endif // !LOGGING_H
       //
