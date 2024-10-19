#include <string.h>
#include <vector>
#include <assert.h>
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

static int32_t read_full(int fd, char *buf, size_t n) {
  while (n > 0) {
    ssize_t rv = read(fd, buf, n);
    if (rv <= 0) {
      return -1;  // error, or unexpected EOF
    }
    assert((size_t)rv <= n);
    n -= (size_t)rv;
    buf += rv;
  }
  return 0;
}

static int32_t write_all(int fd, const char *buf, size_t n) {
  while (n > 0) {
    ssize_t rv = write(fd, buf, n);
    if (rv <= 0) {
      return -1;  // error
    }
    assert((size_t)rv <= n);
    n -= (size_t)rv;
    buf += rv;
  }
  return 0;
}

static int32_t send_req(int fd, const std::vector<std::string> &cmd) {
    uint32_t len = 4;
    for (const std::string &s : cmd) {
        len += 4 + s.size();
    }
    if (len > MAX_MSG_SIZE) {
        return -1;
    }

    char wbuf[4 + MAX_MSG_SIZE];
    memcpy(&wbuf[0], &len, 4);  // assume little endian
    uint32_t n = cmd.size();
    memcpy(&wbuf[4], &n, 4);
    size_t cur = 8;
    for (const std::string &s : cmd) {
        uint32_t p = (uint32_t)s.size();
        memcpy(&wbuf[cur], &p, 4);
        memcpy(&wbuf[cur + 4], s.data(), s.size());
        cur += 4 + s.size();
    }
    return write_all(fd, wbuf, 4 + len);
}

static int32_t read_res(int fd) {
  // 4 bytes header
  char rbuf[HEADER_MSG_SIZE + MAX_MSG_SIZE + 1];
  errno = 0;
  int32_t err = read_full(fd, rbuf, 4);
  if (err) {
    if (errno == 0) {
      msg("EOF");
    } else {
      msg("read() error");
    }
    return err;
  }

  uint32_t len = 0;
  memcpy(&len, rbuf, HEADER_MSG_SIZE);  // assume little endian
  if (len > MAX_MSG_SIZE) {
    msg("too long");
    return -1;
  }

  // reply body
  err = read_full(fd, &rbuf[4], len);
  if (err) {
    msg("read() error");
    return err;
  }

  // print the result
  uint32_t rescode = 0;
  if (len < HEADER_MSG_SIZE) {
    msg("bad response");
    return -1;
  }
  memcpy(&rescode, &rbuf[HEADER_MSG_SIZE], HEADER_MSG_SIZE);
  printf("server says: [%u] %.*s\n", rescode, len - HEADER_MSG_SIZE, &rbuf[8]);
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

  std::vector<std::string> cmd;
  for (int i = 1; i < argc; ++i) {
    cmd.push_back(argv[i]);
  }
  int32_t err = send_req(fd, cmd);
  err = read_res(fd);
  close(fd);
  return 0;
}
