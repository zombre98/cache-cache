#include <cstddef>
#include <assert.h>
#include "hashtable.hpp"

// note: n must be a power of 2
static void h_init(HTab *htab, size_t n) {
  assert(n > 0 && ((n - 1) & n) == 0);
  htab->tab = (HNode **)calloc(sizeof(HNode *), n);
  htab->mask = n - 1;
  htab->size = 0;
}
