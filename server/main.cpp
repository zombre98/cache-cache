#include <cstddef>
#include <assert.h>
#include <cstring>
#include <ostream>
#include <string>
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <cstdint>
#include <iostream>
#include <cstdio>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <iostream>
#include "../lib/lib.hpp"
#include "../lib/hashtable.h"

#define container_of(ptr, type, member) ({                  \
    const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
    (type *)( (char *)__mptr - offsetof(type, member) );})

enum {
  ERR_UNKNOWN = 1,
  ERR_2BIG = 2,
};

enum {
  SER_NIL = 0,
  SER_ERR = 1,
  SER_STR = 2,
  SER_INT = 3,
  SER_ARR = 4,
};

struct Entry {
  struct HNode node;
  std::string key;
  std::string val;
};

static struct {
  HMap db;
} g_data;

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

enum {
  RES_OK = 0,
  RES_ERR = 1,
  RES_NX = 2,
};

static bool entry_eq(HNode *lhs, HNode *rhs) {
  struct Entry *le = container_of(lhs, struct Entry, node);
  struct Entry *re = container_of(rhs, struct Entry, node);

  return le->key == re->key;
}

// FNV-1a hash algorithm
static uint64_t str_hash(const uint8_t *data, size_t len) {
  uint32_t h = 0x811C9DC5;
  for (size_t i = 0; i < len; i++) {
    h = (h + data[i]) * 0x01000193;
  }
  return h;
}

static void out_nil(std::string &out) {
  out.push_back(SER_NIL);
}

static void out_str(std::string &out, const std::string &val) {
  out.push_back(SER_STR);
  uint32_t len = (uint32_t)val.size();
  out.append((char *)&len, 4);
  out.append(val);
}

static void out_int(std::string &out, int64_t val) {
  out.push_back(SER_INT);
  out.append((char *)&val, 8);
}

static void out_err(std::string &out, int32_t code, const std::string &msg) {
  out.push_back(SER_ERR);
  out.append((char *)&code, 4);
  uint32_t len = (uint32_t)msg.size();
  out.append((char *)&len, 4);
  out.append(msg);
}

static void out_arr(std::string &out, uint32_t n) {
  out.push_back(SER_ARR);
  out.append((char *)&n, 4);
}

static void h_scan(HTab *tab, void (*f)(HNode *, void *), void *arg) {
  if (tab->size == 0) {
    return;
  }
  for (size_t i = 0; i <= tab->mask; ++i) {
    HNode *node = tab->tab[i];
    while (node) {
      f(node, arg);
      node = node->next;
    }
  }
}

static void cb_scan(HNode *node, void *arg) {
  std::string &out = *(std::string *)arg;
  out_str(out, container_of(node, Entry, node)->key);
}

static void do_keys(std::vector<std::string> &cmd, std::string &out) {
  // Silent error cmd is not used here since there is no arguments
  (void)cmd;

  out_arr(out, (uint32_t)hm_size(&g_data.db));
  h_scan(&g_data.db.ht1, &cb_scan, &out);
  h_scan(&g_data.db.ht2, &cb_scan, &out);
}

// Look at true command signature from redis
static void do_get(std::vector<std::string> &cmd, std::string &out) {
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
  
  HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (!node) {
    return out_nil(out);
  }
  const std::string &val = container_of(node, Entry, node)->val;
  std::cout << "Get: { key: [" << key.key << "], hcode: [" << key.node.hcode << "], val: [" << val << "] }" << std::endl;
  out_str(out, val);
}

static void do_set(std::vector<std::string> &cmd, std::string &out) {
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
  std::cout << "Set lookup: { key: [" << key.key << "], hcode: [" << key.node.hcode << "] }" << std::endl;

  HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (node) {
    container_of(node, Entry, node)->val.swap(cmd[2]);
  } else {
    Entry *entry = new Entry();
    entry->key.swap(key.key);
    entry->node.hcode = key.node.hcode;
    entry->val.swap(cmd[2]);
    std::cout << "Set: { key: [" << entry->key << "], hcode: [" << entry->node.hcode << "] value: [" << entry->val << "] }" << std::endl;
    hm_insert(&g_data.db, &entry->node);
  }
  out_nil(out);
}

static void do_del(std::vector<std::string> &cmd, std::string &out) {
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = hm_pop(&g_data.db, &key.node, &entry_eq);
  if (node) {
    delete container_of(node, Entry, node);
  }
  // Indicate if deletion took place
  out_int(out, node ? 1 : 0);
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

static int32_t parse_req(const uint8_t *data, size_t len, std::vector<std::string> &out) {
    if (len < HEADER_MSG_SIZE) {
        return -1;
    }
    uint32_t n = 0;
    memcpy(&n, &data[0], HEADER_MSG_SIZE);
    if (n > MAX_MSG_SIZE) {
        return -1;
    }

    size_t pos = HEADER_MSG_SIZE;
    while (n--) {
        if (pos + HEADER_MSG_SIZE > len) {
            return -1;
        }
        uint32_t sz = 0;
        memcpy(&sz, &data[pos], HEADER_MSG_SIZE);
        if (pos + HEADER_MSG_SIZE + sz > len) {
            return -1;
        }
        out.push_back(std::string((char *)&data[pos + HEADER_MSG_SIZE], sz));
        pos += HEADER_MSG_SIZE + sz;
    }

    if (pos != len) {
        return -1;  // trailing garbage
    }
    return 0;
}

static void do_request(std::vector<std::string> &cmd, std::string &out) {
  if (cmd.size() == 1 && cmd_is(cmd[0], "keys")) {
    do_keys(cmd, out);
  } else if (cmd.size() == 2 && cmd_is(cmd[0], "get")) {
    do_get(cmd, out);
  } else if (cmd.size() == 3 && cmd_is(cmd[0], "set")) {
    do_set(cmd, out);
  } else if (cmd.size() == 2 && cmd_is(cmd[0], "del")) {
    do_del(cmd, out);
  } else {
    out_err(out, ERR_UNKNOWN, "Unknown cmd");
  }
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

  std::vector<std::string> cmd;
  if (parse_req(&conn->rbuf[HEADER_MSG_SIZE], len, cmd) != 0) {
    msg("Bad request");
    conn->state = STATE_END;
    return false;
  } 
  
  // Need better managing of buffer instead of string
  std::string out;
  do_request(cmd, out);
  if (HEADER_MSG_SIZE + out.size() > MAX_MSG_SIZE) {
    out.clear();
    out_err(out, ERR_2BIG, "response is too big");
  }
  uint32_t wlen = (uint32_t)out.size();
  memcpy(&conn->wbuf[0], &wlen, HEADER_MSG_SIZE);
  memcpy(&conn->wbuf[HEADER_MSG_SIZE], out.data(), out.size());
  conn->wbuf_size = HEADER_MSG_SIZE + wlen;

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
