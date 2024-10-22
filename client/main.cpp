#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string.h>
#include <sys/types.h>
#include <vector>
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

enum {
  SER_NIL = 0,
  SER_ERR = 1,
  SER_STR = 2,
  SER_INT = 3,
  SER_ARR = 4,
};

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
    return write_n(fd, wbuf, 4 + len);
}

static int32_t on_response(const uint8_t *data, size_t size) {
  if (size < 1) {
    msg("bad response");
    return -1;
  }
  if (data[0] == SER_NIL) {
    printf("(nil)\n");
    return 1;
  } 
  if (data[0] == SER_ERR) {
    if (size < 9) {
      msg("bad response");
      return -1;
    }
    int32_t code = 0;
    uint32_t len = 0;
    memcpy(&code, &data[1], 4);
    memcpy(&len, &data[5], 4);
    if (size < 9 + len) {
      msg("bad response");
      return -1;
    }
    printf("(err) %d %.*s\n", code, len, &data[9]);
    return 9 + len;
  }
  if (data[0] == SER_STR) {
    if (size < 5) {
      msg("bad response");
      return -1;
    }
    uint32_t len = 0;
    memcpy(&len, &data[1], 4);
    if (size < 5 + len) {
      msg("bad response");
      return -1;
    }
    printf("(str) %.*s\n", len, &data[5]);
    return 5 + len;
  }
  if (data[0] == SER_INT) {
    if (size < 9) {
      msg("bad response");
      return -1;
    }
    int64_t val = 0;
    memcpy(&val, &data[1], 8);
    printf("(int) %ld\n", val);
    return 9;
  }
  if (data[0] == SER_ARR) {
    if (size < 5) {
      msg("bad response");
      return -1;
    }
    uint32_t len = 0;
    memcpy(&len, &data[1], 4);
    printf("(arr) len=%u\n", len);
    size_t arr_bytes = 1 + 4;
    for (uint32_t i = 0; i < len; ++i) {
      int32_t rv = on_response(&data[arr_bytes], size - arr_bytes);
        if (rv < 0) {
          return rv;
        }
        arr_bytes += (size_t)rv;
      }
    printf("(arr) end\n");
    return (int32_t)arr_bytes;
  }
  msg("bad response");
  return -1;
}

static int32_t read_res(int fd) {
  // 4 bytes header
  char rbuf[HEADER_MSG_SIZE + MAX_MSG_SIZE + 1];
  errno = 0;
  int32_t err = read_n(fd, rbuf, 4);
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
  err = read_n(fd, &rbuf[4], len);
  if (err) {
    msg("read() error");
    return err;
  }

  // print the result
  int32_t rv = on_response((uint8_t *)&rbuf[HEADER_MSG_SIZE], len);
  if (rv > 0 && (uint32_t)rv != len) {
    msg("bad response");
    rv = -1;
  }
  return rv;
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
