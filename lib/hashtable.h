#pragma once 

#include <cstdint>
#include <stddef.h>
#include <stdint.h>

struct HNode {
  HNode *next = NULL;
  uint64_t hcode = 0; // Hash value used for resizing anc checking
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

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
void hm_insert(HMap *hmap, HNode *node);
HNode *hm_pop(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
size_t hm_size(HMap *hmap);
void hm_destroy(HMap *hmap);
