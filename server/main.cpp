#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include "../lib/lib.hpp"

const size_t MAX_MSG_SIZE = 4096;
const size_t HEADER_MSG_SIZE = 4; // SIZE of an integer that store message size 

static int handle_request(int connfd) {
  char rbuf[HEADER_MSG_SIZE + MAX_MSG_SIZE + 1];
  int32_t err = read_n(connfd, rbuf, HEADER_MSG_SIZE);
  if (err) {
    msg("read message header error");
    return err;
  }

  uint32_t len = 0;
  memcpy(&len, rbuf, HEADER_MSG_SIZE);  // assume little endian
  printf("Sent message size: [%d]\n", len);
  if (len > MAX_MSG_SIZE) {
    msg("too long");
    return -1;
  }

  err = read_n(connfd, &rbuf[HEADER_MSG_SIZE], len);
  if (err) {
    msg("read message body error");
    return err;
  }

  rbuf[HEADER_MSG_SIZE + len] = '\0';
  printf("client says: [%d][%s]\n", len, &rbuf[HEADER_MSG_SIZE]);

  const char reply[] = "world";
  char wbuf[HEADER_MSG_SIZE + sizeof(reply)];
  len = (uint32_t)strlen(reply);
  memcpy(wbuf, &len, HEADER_MSG_SIZE);
  memcpy(&wbuf[4], reply, len);

  printf("Will send an answer of: %s\n", wbuf);
  return write_n(connfd, wbuf, MAX_MSG_SIZE + len);
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
