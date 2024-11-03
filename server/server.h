#pragma once

#include "../lib/zset.h"
#include "../lib/list.h"
#include <string>

enum {
  ERR_UNKNOWN = 1,
  ERR_2BIG = 2,
  ERR_TYPE = 3,
  ERR_ARG = 4,
};

enum {
  T_STR = 0,
  T_ZSET = 1,
};

enum {
  STATE_REQ = 0,
  STATE_RES = 1,
  STATE_END = 2,  // mark the connection for deletion
};

enum {
  RES_OK = 0,
  RES_ERR = 1,
  RES_NX = 2,
};


const size_t MAX_MSG_SIZE = 4096;
const size_t HEADER_MSG_SIZE = 4; // SIZE of an integer that store message size 
const uint64_t IDLE_TIMEOUT_MS = 5 * 1000;
const size_t CONTAINER_SIZE = 10000;

// Make val an union to reduce allocations
struct Entry {
  struct HNode node;
  std::string key;
  uint32_t type = 0;
  std::string val;
  ZSet *zset = NULL;
  // TTL real redis does not use sorting for expiration
  size_t heap_idx = -1;
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

  uint64_t idle_start = 0;
  DList idle_list;
};


