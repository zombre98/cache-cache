#include <string>
#include <string.h>

bool cmd_is(const std::string &word, const char *cmd) {
  return 0 == strcasecmp(word.c_str(), cmd);
}

