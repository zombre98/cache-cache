#pragma once 

#include <cstddef>
#include <string>
#include <sys/types.h>

struct HNode {
  HNode *next = NULL;
  uint hcode = 0; // Hash value used for resizing anc checking
};

struct HTab {
  HNode **tab = NULL; 
  size_t mask = 0; // 2^n -1
  size_t size = 0;
};

struct HMap {
  HTab ht1; // newer
  HTab ht2; // older
  size_t resizing_pos = 0;
};

struct Entry {
  struct HNode node;
  std::string key;
  std::string value;
};

void hm_insert(HMap *, HNode *);
HNode *hm_lookup(HMap *, HNode *, bool (*eq)(HNode *, HNode *));
HNode *hm_pop(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
size_t hm_size(HMap *hmap);
void hm_destroy(HMap *hmap);
