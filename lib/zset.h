#include "avl.h"
#include "hashtable.h"

// Sorted set is indexed in 2 ways
// Hashmap by name. The primay keu
// Sorted by the (score, name) pair. The secondary index
struct ZSet {
  AVLNode *tree = NULL;
  HMap hmap;
};

struct ZNode {
  AVLNode tree; // index by (score, name)
  HNode hmap; // index by name
  double score;
  size_t len;
  char name[0];
};

bool zset_add(ZSet *zset, const char *name, size_t len, double score);
ZNode *zset_lookup(ZSet *zset, const char *name, size_t len);
ZNode *zset_pop(ZSet *zset, const char *name, size_t len);
ZNode *zset_query(ZSet *zset, double score, const char *name, size_t len);
void zset_dispose(ZSet *zset);
ZNode *znode_offset(ZNode *node, int64_t offset);
void znode_del(ZNode *node);


