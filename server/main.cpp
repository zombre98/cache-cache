#include <cstddef>
#include <cstdint>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <assert.h>
#include "../lib/logging.hpp"

const size_t MAX_MSG_SIZE = 4096;
const size_t HEADER_MSG_SIZE = 4; // SIZE of an integer that store message size 

static int32_t read_n(int fd, char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0) {
            return -1; 
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t write_n(int fd, const char *buf, size_t n) {
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

static int handle_request(int connfd) {
  char rbuf[HEADER_MSG_SIZE + MAX_MSG_SIZE + 1];
  int32_t err = read_n(connfd, rbuf, HEADER_MSG_SIZE);
  if (err) {
    msg("read message header error");
    return err;
  }

  uint32_t len = 0;
  memcpy(&len, rbuf, HEADER_MSG_SIZE);  // assume little endian
  if (len > MAX_MSG_SIZE) {
    msg("too long");
    return -1;
  }

  err = read_n(connfd, &rbuf[4], len);
  if (err) {
    msg("read message body error");
    return err;
  }

  rbuf[4 + len] = '\0';
  printf("client says: [%d][%s]\n", len, &rbuf[4]);

  const char reply[] = "world";
  char wbuf[4 + sizeof(reply)];
  uint32_t answer_len = (uint32_t)strlen(reply);
  memcpy(wbuf, &answer_len, 4);
  memcpy(&wbuf[4], reply, answer_len);

  return write_n(connfd, wbuf, MAX_MSG_SIZE + answer_len);
}

int main (int argc, char *argv[]) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    die("socket()");
  }

  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

  struct sockaddr_in address = {};
  address.sin_family = AF_INET;
  // TODO: Take port as parameters 
  address.sin_port = ntohs(1234);
  address.sin_addr.s_addr = ntohl(0);

  int rv = bind(fd, (const sockaddr *)&address, sizeof(address));
  if (rv) {
    die("bind()");
  }

  rv = listen(fd, SOMAXCONN);
  if (rv) {
    die("listen()");
  }

  while(true) {
    struct sockaddr_in client_address = {};
    socklen_t address_length = sizeof(client_address);
    int connfd = accept(fd, (struct sockaddr *)&client_address, &address_length);
    if (connfd < 0) {
      continue;
    }

    while (true) {
      int error = handle_request(connfd);
      if (error) {
        break;
      }
    }
    close(connfd);
  }

  return 0;
}
