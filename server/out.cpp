#include "../lib/common.h"
#include <assert.h>
#include <string>

void out_str(std::string &out, const char *s, size_t size) {
  out.push_back(SER_STR);
  uint32_t len = (uint32_t)size;
  out.append((char *)&len, 4);
  out.append(s, len);
}

void out_str(std::string &out, const std::string &val) {
  out.push_back(SER_STR);
  uint32_t len = (uint32_t)val.size();
  out.append((char *)&len, 4);
  out.append(val);
}

void out_int(std::string &out, int64_t val) {
  out.push_back(SER_INT);
  out.append((char *)&val, 8);
}

void out_dbl(std::string &out, double val) {
  out.push_back(SER_DBL);
  out.append((char *)&val, 8);
}

void out_err(std::string &out, int32_t code, const std::string &msg) {
  out.push_back(SER_ERR);
  out.append((char *)&code, 4);
  uint32_t len = (uint32_t)msg.size();
  out.append((char *)&len, 4);
  out.append(msg);
}

void out_arr(std::string &out, uint32_t n) {
  out.push_back(SER_ARR);
  out.append((char *)&n, 4);
}

void *begin_arr(std::string &out) {
  out.push_back(SER_ARR);
  out.append("\0\0\0\0", 4);
  return (void *)(out.size() - 4);
}

void end_arr(std::string &out, void *ctx, uint32_t n) {
  size_t pos = (size_t)ctx;
  assert(out[pos - 1] == SER_ARR);
  memcpy(&out[pos], &n, 4);
}


