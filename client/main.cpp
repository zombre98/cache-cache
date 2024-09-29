#include <string.h>
#include <cstdio>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include "../lib/lib.hpp"

const size_t MAX_MSG_SIZE = 4096;
const size_t HEADER_MSG_SIZE = 4;

static int32_t query(int fd, const char *text) {
  uint32_t len = (uint32_t)strlen(text);
  if (len > MAX_MSG_SIZE) {
    return -1;
  }

  char wbuf[4 + MAX_MSG_SIZE];
  memcpy(wbuf, &len, HEADER_MSG_SIZE);  // assume little endian
  memcpy(&wbuf[4], text, len);
  if (int32_t err = write_n(fd, wbuf, HEADER_MSG_SIZE + len)) {
     return err;
  }

  char rbuf[HEADER_MSG_SIZE + MAX_MSG_SIZE + 1];
  int32_t err = read_n(fd, rbuf, HEADER_MSG_SIZE);
  if (err) {
    msg("read() error");
    return err;
  }

  memcpy(&len, rbuf, HEADER_MSG_SIZE);  // assume little endian
  if (len > MAX_MSG_SIZE) {
    msg("too long");
    return -1;
  }

  err = read_n(fd, &rbuf[HEADER_MSG_SIZE], len);
  if (err) {
    msg("read() error");
    return err;
  }

  // do something
  rbuf[HEADER_MSG_SIZE + len] = '\0';
  printf("server says: [%d][%s]\n", len, &rbuf[HEADER_MSG_SIZE]);
  return 0;
}

int main (int argc, char *argv[]) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    die("socket()");
  }

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(1234);
  addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);
  int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
  if (rv) {
    die("connect()");
  }

  query(fd, "Hello");
  query(fd, "Are");
  query(fd, "GN");
  close(fd);
  return 0;
}
