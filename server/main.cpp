#include <cmath>
#include <cstddef>
#include <assert.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
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
#include "../lib/common.h"
#include "../lib/heap.h"
#include "../lib/thread_pool.h"
#include "out.h"
#include "server.h"
#include "timer.h"


static struct {
  HMap db;
  std::vector<Conn *> fd2conn;
  DList idle_list;
  std::vector<HeapItem> heap;
  ThreadPool tp;
} g_data;

static void entry_set_ttl(Entry *ent, int64_t ttl_ms) {
  if (ttl_ms < 0 && ent->heap_idx != -1) {
    // Erase and item from the heap by replicing it with last item in array
    size_t pos = ent->heap_idx;
    g_data.heap[pos] = g_data.heap.back();
    g_data.heap.pop_back();
    if (pos < g_data.heap.size()) {
      heap_update(g_data.heap.data(), pos, g_data.heap.size());
    }
    ent->heap_idx = -1;
  } else if (ttl_ms >= 0) {
    size_t pos = ent->heap_idx;
    if (pos == -1) {
      HeapItem item;
      item.ref = &ent->heap_idx;
      g_data.heap.push_back(item);
      pos = g_data.heap.size() - 1;
    }
    g_data.heap[pos].val = get_monotonic_usec() + ttl_ms * 1000;
    heap_update(g_data.heap.data(), pos, g_data.heap.size());
  }
}

static bool str2dbl(const std::string &s, double &out) {
  char *endp = NULL;
  out = strtod(s.c_str(), &endp);
  return endp == s.c_str() + s.size() && !std::isnan(out);
}

static bool str2int(const std::string &s, int64_t &out) {
  char *endp = NULL;
  out = strtoll(s.c_str(), &endp, 10);
  return endp == s.c_str() + s.size();
}

static bool entry_eq(HNode *lhs, HNode *rhs) {
  struct Entry *le = container_of(lhs, struct Entry, node);
  struct Entry *re = container_of(rhs, struct Entry, node);

  return le->key == re->key;
}

static uint32_t next_timer_ms() {
  uint64_t now_us = get_monotonic_usec();
  uint64_t next_us = -1;

  // idle timers
  if (!dlist_empty(&g_data.idle_list)) {
    Conn *next = container_of(g_data.idle_list.next, Conn, idle_list);
    next_us = next->idle_start + IDLE_TIMEOUT_MS * 1000;
  }

  // ttl timers
  if (!g_data.heap.empty() && g_data.heap[0].val < next_us) {
    next_us = g_data.heap[0].val;
  }

  if (next_us == -1) {
    return 1000;
  }
  if (next_us <= now_us) {
    return 0;
  }

  return (uint32_t)((next_us - now_us) / 1000);
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

static void do_expire(std::vector<std::string> &cmd, std::string &out) {
  int64_t ttl_ms = 0;
  if (!str2int(cmd[2], ttl_ms)) {
    return out_err(out, ERR_ARG, "expect int64");
  }

  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (node) {
    Entry *ent = container_of(node, Entry, node);
    entry_set_ttl(ent, ttl_ms);
  }
  return out_int(out, node ? 1 : 0);
}

static void do_ttl(std::vector<std::string> &cmd, std::string &out) {
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (!node) {
    return out_int(out, -2);
  }

  Entry *ent = container_of(node, Entry, node);
  if (ent->heap_idx == -1) {
    return out_int(out, -1);
  }

  uint64_t expire_at = g_data.heap[ent->heap_idx].val;
  int64_t now = get_monotonic_usec();
  return out_int(out, expire_at > now ? (expire_at - now) / 1000 : 0);
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

static void entry_destroy(Entry *ent) {
  if (ent->type == T_ZSET) {
    zset_dispose(ent->zset);
    delete ent->zset;
  }
  entry_set_ttl(ent, -1);
  delete ent;
}

static void entry_del_async(void *arg) {
  entry_destroy((Entry *)arg);
}

static void entry_del(Entry *ent) {
  entry_set_ttl(ent, -1);

  bool too_big = false;
  if (ent->type == T_ZSET) {
    too_big = hm_size(&ent->zset->hmap) > CONTAINER_SIZE;
  }
  
  if (too_big) {
    thread_pool_queue(&g_data.tp, &entry_del_async, ent);
  } else {
    entry_destroy(ent);
  }
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

static void do_zadd(std::vector<std::string> &cmd, std::string &out) {
  double score = 0;
  if (!str2dbl(cmd[2], score)) {
    return out_err(out, ERR_ARG, "expect fp number");
  }

  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
  HNode *hnode = hm_lookup(&g_data.db, &key.node, &entry_eq);

  Entry *ent = NULL;
  if (!hnode) {
    ent = new Entry();
    ent->key.swap(key.key);
    ent->node.hcode = key.node.hcode;
    ent->type = T_ZSET;
    ent->zset = new ZSet();
    hm_insert(&g_data.db, &ent->node);
  } else {
    ent = container_of(hnode, Entry, node);
    if (ent->type != T_ZSET) {
      return out_err(out, ERR_TYPE, "expect zset");
    }
  }

  const std::string &name = cmd[3];
  bool added = zset_add(ent->zset, name.data(), name.size(), score);
  return out_int(out, (int64_t)added);
}

static bool expect_zset(std::string &out, std::string &s, Entry **ent) {
  Entry key;
  key.key.swap(s);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
  HNode *hnode = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (!hnode) {
    out_nil(out);
    return false;
  }

  *ent = container_of(hnode, Entry, node);
  if ((*ent)->type != T_ZSET) {
    out_err(out, ERR_TYPE, "expect zset");
    return false;
  }
  return true;
}

static void do_zrem(std::vector<std::string> &cmd, std::string &out) {
  Entry *ent = NULL;
  if (!expect_zset(out, cmd[1], &ent)) {
    return;
  }

  const std::string &name = cmd[2];
  ZNode *znode = zset_pop(ent->zset, name.data(), name.size());
  if (znode) {
    znode_del(znode);
  }
  return out_int(out, znode ? 1 : 0);
}

static void do_zscore(std::vector<std::string> &cmd, std::string &out) {
  Entry *ent = NULL;
  if (!expect_zset(out, cmd[1], &ent)) {
    return;
  }

  const std::string &name = cmd[2];
  ZNode *znode = zset_lookup(ent->zset, name.data(), name.size());
  return znode ? out_dbl(out, znode->score) : out_nil(out);
}

static void do_zquery(std::vector<std::string> &cmd, std::string &out) {
  double score = 0;
  if (!str2dbl(cmd[2], score)) {
    return out_err(out, ERR_ARG, "expect fp number");
  }

  const std::string &name = cmd[3];
  int64_t offset = 0;
  int64_t limit = 0;
  if (!str2int(cmd[4], offset)) {
    return out_err(out, ERR_ARG, "expect int");
  }
  if (!str2int(cmd[5], limit)) {
    return out_err(cmd[5], ERR_ARG, "expect int");
  }

  Entry *ent = NULL;
  if (!expect_zset(out, cmd[1], &ent)) {
    if (out[0] == SER_NIL) {
      out.clear();
      out_arr(out, 0);
    }
    return;
  }
  if (limit <= 0) {
    return out_arr(out, 0);
  }
  ZNode *znode = zset_query(ent->zset, score, name.data(), name.size());
  znode = znode_offset(znode, offset);

  void *arr = begin_arr(out);
  uint32_t n = 0;
  while (znode && (int64_t)n < limit) {
    out_str(out, znode->name, znode->len);
    out_dbl(out, znode->score);
    znode = znode_offset(znode, +1);
    n += 2;
  }
  end_arr(out, arr, n);
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

static void conn_done(Conn *conn) {
  g_data.fd2conn[conn->fd] = NULL;
  close(conn->fd);
  dlist_detach(&conn->idle_list);
  free(conn);
}

int32_t accept_new_connection(int fd) {
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
  conn->idle_start = get_monotonic_usec();
  dlist_insert_before(&g_data.idle_list, &conn->idle_list);
  conn_put(g_data.fd2conn, conn);
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
  } else if (cmd.size() == 3 && cmd_is(cmd[0], "pexpire")) {
    do_expire(cmd, out);
  } else if (cmd.size() == 2 && cmd_is(cmd[0], "pttl")) {
    do_ttl(cmd, out);
  } else if (cmd.size() == 4 && cmd_is(cmd[0], "zadd")) {
    do_zadd(cmd, out);
  } else if (cmd.size() == 3 && cmd_is(cmd[0], "zrem")) {
    do_zrem(cmd, out);
  } else if (cmd.size() == 3 && cmd_is(cmd[0], "zscore")) {
    do_zscore(cmd, out);
  } else if (cmd.size() == 6 && cmd_is(cmd[0], "zquery")) {
    do_zquery(cmd, out);
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

static bool hnode_same(HNode *lhs, HNode *rhs) {
  return lhs == rhs;
}

static void process_timers() {
  uint64_t now_us = get_monotonic_usec() + 1000;
  while (!dlist_empty(&g_data.idle_list)) {
    Conn *next = container_of(g_data.idle_list.next, Conn, idle_list);
    uint64_t next_us = next->idle_start + IDLE_TIMEOUT_MS * 1000;
    if (next_us >= now_us + 1000) {
      // not ready the extrat 1000us is for the ms resolution of poll()
      break;
    }
    printf("removing idle connection %d\n", next->fd);
    conn_done(next);
  }

  const size_t k_max_works = 2000;
  size_t n_works = 0;
  while (!g_data.heap.empty() && g_data.heap[0].val < now_us) {
    Entry *ent = container_of(g_data.heap[0].ref, Entry, heap_idx);
    HNode *node = hm_pop(&g_data.db, &ent->node, &hnode_same);
    assert(node == &ent->node);
    entry_del(ent);
    if (n_works++ >= k_max_works) {
      break;
    }
  }
}

/*
 * Wake up by poll, update the idel timer bu moving conn to the end of the list.
 */
static void connection_io(Conn *conn) {
  conn->idle_start = get_monotonic_usec();
  dlist_detach(&conn->idle_list);
  dlist_insert_before(&g_data.idle_list, &conn->idle_list);

  if (conn->state == STATE_REQ) {
    state_req(conn);
  } else if (conn->state == STATE_RES) {
    state_res(conn);
  } else {
    assert(0);  // not expected
  }
}

int main(int argc, char *argv[]) {
  dlist_init(&g_data.idle_list);
  thread_pool_init(&g_data.tp, 4);

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

  non_blocking_fd(fd);

  std::vector<struct pollfd> poll_args;
  while(true) {
    poll_args.clear();
    
    struct pollfd pfd = {fd, POLLIN, 0};
    poll_args.push_back(pfd);
    
    for (Conn *conn : g_data.fd2conn) {
      if (!conn) {
        continue;
      }
      struct pollfd pfd = {};
      pfd.fd = conn->fd;
      pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
      pfd.events = pfd.events | POLLERR;
      poll_args.push_back(pfd);
    }

    int timeout_ms = (int)next_timer_ms();
    int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), timeout_ms);
    if (rv < 0) {
      die("poll");
    }

    for (size_t i = 1; i < poll_args.size(); ++i) {
      if (poll_args[i].revents) {
        Conn *conn = g_data.fd2conn[poll_args[i].fd];
        connection_io(conn);
        if (conn->state == STATE_END) {
          // Client closed normally or something bad happened.
          conn_done(conn);
        }
      }
    }
    
    process_timers();

    if (poll_args[0].revents) {
      accept_new_connection(fd);
    }
  }
  return 0;
}
