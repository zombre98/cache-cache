#include <string.h>
#include <cstdio>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include "../lib/logging.hpp"

static void handle_connection(int connfd) {
  char rbuf[64] = {};
  ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
  if (n < 0) {
    msg("read() error");
    return;
  }
  printf("client says: %s\n", rbuf);

  const char *response = "world";
  write(connfd, response, strlen(response));
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

    handle_connection(connfd);
    close(connfd);
  }

  return 0;
}
