#pragma once 

#include <cstddef>
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



