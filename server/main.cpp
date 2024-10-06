#include <cstddef>
#include <assert.h>
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
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

enum {
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2,  // mark the connection for deletion
};

struct Conn {
    int fd = -1;
    uint32_t state = 0;     // either STATE_REQ or STATE_RES
    
    // buffer for reading
    size_t rbuf_size = 0;
    uint8_t rbuf[HEADER_MSG_SIZE + MAX_MSG_SIZE];
    
    // buffer for writing
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[HEADER_MSG_SIZE + MAX_MSG_SIZE];
};


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

  return write_n(connfd, wbuf, HEADER_MSG_SIZE + len);
}

// Set the file descriptor to non blocking mode
static void non_blocking_fd(int fd) {
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno) {
        die("fcntl error");
        return;
    }

    flags |= O_NONBLOCK;

    errno = 0;
    (void)fcntl(fd, F_SETFL, flags);
    if (errno) {
        die("fcntl error");
    }
}

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn) {
  if (fd2conn.size() <= (size_t)conn->fd) {
    fd2conn.resize(conn->fd + 1);
  }
  fd2conn[conn->fd] = conn;
}

int32_t accept_new_connection(std::vector<Conn *> &fd2conn, int fd) {
  struct sockaddr_in client_addr = {};
  socklen_t socklen = sizeof(client_addr);
  int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
  if (connfd < 0) {
    msg("accept() error");
    return -1;  // error
  }

  non_blocking_fd(connfd);

  struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
  if (!conn) {
     close(connfd);
     return -1;
  }
  conn->fd = connfd;
  conn->state = STATE_REQ;
  conn->rbuf_size = 0;
  conn->wbuf_size = 0;
  conn->wbuf_sent = 0;
  conn_put(fd2conn, conn);
  return 0;
}

static bool try_flush_buffer(Conn *conn) {
    ssize_t rv = 0;
    do {
        size_t remain = conn->wbuf_size - conn->wbuf_sent;
        rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
    } while (rv < 0 && errno == EINTR);
    if (rv < 0 && errno == EAGAIN) {
        // got EAGAIN, stop.
        return false;
    }
    if (rv < 0) {
        msg("write() error");
        conn->state = STATE_END;
        return false;
    }
    conn->wbuf_sent += (size_t)rv;
    assert(conn->wbuf_sent <= conn->wbuf_size);
    if (conn->wbuf_sent == conn->wbuf_size) {
        // response was fully sent, change state back
        conn->state = STATE_REQ;
        conn->wbuf_sent = 0;
        conn->wbuf_size = 0;
        return false;
    }
    // still got some data in wbuf, could try to write again
    return true;
}

static void state_res(Conn *conn) {
    while (try_flush_buffer(conn)) {}
}

static bool try_one_request(Conn *conn) {
  // try to parse a request from the buffer
  if (conn->rbuf_size < HEADER_MSG_SIZE) {
     return false;
  }
  
  uint32_t len = 0;
  memcpy(&len, &conn->rbuf[0], HEADER_MSG_SIZE);
  if (len > MAX_MSG_SIZE) {
    msg("too long");
    conn->state = STATE_END;
    return false;
  }
  if (HEADER_MSG_SIZE + len > conn->rbuf_size) {
    // not enough data in the buffer. Will retry in the next iteration
    return false;
  }  
  printf("client says: %.*s\n", len, &conn->rbuf[HEADER_MSG_SIZE]);

  // generating echoing response
  memcpy(&conn->wbuf[0], &len, HEADER_MSG_SIZE);
  memcpy(&conn->wbuf[HEADER_MSG_SIZE], &conn->rbuf[HEADER_MSG_SIZE], len);
  conn->wbuf_size = HEADER_MSG_SIZE + len;

  // remove the request from the buffer.
  // note: frequent memmove is inefficient.
  // note: need better handling for production code.
  size_t remain = conn->rbuf_size - HEADER_MSG_SIZE - len;
  if (remain) {
    memmove(conn->rbuf, &conn->rbuf[HEADER_MSG_SIZE + len], remain);
  }
  conn->rbuf_size = remain;

  // change state
  conn->state = STATE_RES;
  state_res(conn);

  // continue the outer loop if the request was fully processed
  return (conn->state == STATE_REQ);
}

static bool try_fill_buffer(Conn *conn) {
  // try to fill the buffer
  assert(conn->rbuf_size < sizeof(conn->rbuf));
  ssize_t rv = 0;
  do {
      size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
      rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
  } while (rv < 0 && errno == EINTR);
    
  if (rv < 0 && errno == EAGAIN) {
    return false;
  }
  if (rv < 0) {
    msg("read() error");
    conn->state = STATE_END;
    return false;
  }
  if (rv == 0) {
    if (conn->rbuf_size > 0) {
      msg("unexpected EOF");
    } else {
      msg("EOF");
    }
    conn->state = STATE_END;
    return false;
  }

  conn->rbuf_size += (size_t)rv;
  assert(conn->rbuf_size <= sizeof(conn->rbuf));

  // Try to process requests one by one.
  // Why is there a loop? Please read the explanation of "pipelining".
  while (try_one_request(conn)) {}
  return (conn->state == STATE_REQ);
}

static void state_req(Conn *conn) {
  while (try_fill_buffer(conn)) {}
}

static void connection_io(Conn *conn) {
    if (conn->state == STATE_REQ) {
        state_req(conn);
    } else if (conn->state == STATE_RES) {
        state_res(conn);
    } else {
        assert(0);  // not expected
    }
}

int main(int argc, char *argv[]) {
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

  // Is it better to use a map or unordered_map ?
  std::vector<Conn *> fd2conn;

  non_blocking_fd(fd);

  std::vector<struct pollfd> poll_args;
  while(true) {
    poll_args.clear();
    
    struct pollfd pfd = {fd, POLLIN, 0};
    poll_args.push_back(pfd);
    
    for (Conn *conn : fd2conn) {
      if (!conn) {
        continue;
      }
      struct pollfd pfd = {};
      pfd.fd = conn->fd;
      pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
      pfd.events = pfd.events | POLLERR;
      poll_args.push_back(pfd);
    }

    int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
    if (rv < 0) {
      die("poll");
    }

    for (size_t i = 1; i < poll_args.size(); ++i) {
      if (poll_args[i].revents) {
        Conn *conn = fd2conn[poll_args[i].fd];
        connection_io(conn);
        if (conn->state == STATE_END) {
          fd2conn[conn->fd] = NULL;
          close(conn->fd);
          free(conn);
        }
      }
    }
    if (poll_args[0].revents) {
      accept_new_connection(fd2conn, fd);
    }
  }
  return 0;
}
