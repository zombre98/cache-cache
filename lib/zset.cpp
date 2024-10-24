#include "zset.h"
#include "common.h"
#include "hashtable.h"
#include "avl.h"
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string.h>

struct HKey {
  HNode node;
  size_t len = 0;
  const char *name = NULL;
};

static bool hmcp(HNode *node, HNode *key) {
  // Wondering from where hmap comes from
  ZNode *znode = container_of(node, ZNode, hmap);
  HKey *hkey = container_of(key, HKey, node);

  if (znode->len != hkey->len) {
    return false;
  }
  return memcmp(znode->name, hkey->name, znode->len) == 0;
}

static bool min(size_t left, size_t right) {
  return left < right ? left : right;
}

static bool zless(AVLNode *left, double score, const char *name, size_t len) {
  ZNode *zl = container_of(left, ZNode, tree);
  if (zl->score != score) {
    return zl->score < score;
  }
  int rv = memcmp(zl->name, name, min(zl->len, len));
  if (rv != 0) {
    return rv < 0;
  }
  return zl->len < len;
}

static bool zless(AVLNode *left, AVLNode *right) {
  ZNode *zr = container_of(right, ZNode, tree);
  return zless(left, zr->score, zr->name, zr->len);
}

static ZNode *znode_new(const char *name, size_t len, double score) {
  ZNode *node = (ZNode *)malloc(sizeof(ZNode) + len);
  avl_init(&node->tree);
  node->hmap.next = NULL;
  node->hmap.hcode = str_hash((uint8_t *)name, len);
  node->score = score;
  node->len = len;
  memcpy(&node->name[0], name, len);
  return node;
}

static void tree_add(ZSet *zset, ZNode *node) {
  AVLNode *curr = NULL;
  AVLNode **from = &zset->tree;
  while (*from) {
    curr = *from;
    from = zless(&node->tree, curr) ? &curr->left : &curr->right;
  }
  *from = &node->tree;
  node->tree.parent = curr;
  zset->tree = avl_fix(&node->tree);
}

/*
 * Lookup by name is just a hashtable Lookup
 */
ZNode *zset_lookup(ZSet *zset, const char *name, size_t len) {
  if (!zset->tree) {
    return NULL;
  }
  HKey key;
  key.node.hcode = str_hash((uint8_t *)name, len);
  key.name = name;
  key.len = len;
  HNode *found = hm_lookup(&zset->hmap, &key.node, &hmcp);
  return found ? container_of(found, ZNode, hmap) : NULL;
}

/*
 * Detach and reinster the node to keep it ordered when score change
 */
static void zset_update(ZSet *zset, ZNode *node, double score) {
  if (node->score == score) {
    return;
  }
  zset->tree = avl_del(&node->tree);
  node->score = score;
  avl_init(&node->tree);
  tree_add(zset, node);
}

/*
 * zset_add add a tuple (name, score) or update the existing name with the score
 */
bool zset_add(ZSet *zset, const char *name, size_t len, double score) {
  ZNode *node = zset_lookup(zset, name, len);
  if (node) {
    zset_update(zset, node, score);
    return false;
  }

  node = znode_new(name, len, score);
  hm_insert(&zset->hmap, &node->hmap);
  tree_add(zset, node);
  return true;
}

/*
 * Lokup and detach a node by name first search in hmap (O(1)) and then in tree (O(log(n))) faster if it does not exist
 */
ZNode *zset_pop(ZSet *zset, const char *name, size_t len) {
  if (!zset->tree) {
    return NULL;
  }

  HKey key;
  key.node.hcode = str_hash((uint8_t *)name, len);
  key.name = name;
  key.len = len;
  HNode *found = hm_pop(&zset->hmap, &key.node, &hmcp);
  if (!found) {
    return NULL;
  }

  ZNode *node = container_of(found, ZNode, hmap);
  zset->tree = avl_del(&node->tree);
  return node;
}

/*
 * Deallocate the node
 */
void znode_del(ZNode *node) {
  free(node);
}

ZNode *zset_query(ZSet *zset, double score, const char *name, size_t len) {
  AVLNode *found = NULL;
  for (AVLNode *curr = zset->tree; curr;) {
    if (zless(curr, score, name, len)) {
      curr = curr->right;
    } else {
      found = curr;
      curr = curr->left;
    }
  }
  return found ? container_of(found, ZNode, tree) : NULL;
}

ZNode *znode_offset(ZNode *node, int64_t offset) {
  AVLNode *tnode = node ? avl_offset(&node->tree, offset) : NULL;
  return tnode ? container_of(tnode, ZNode, tree) : NULL;
}

static void tree_dispose(AVLNode *node) {
  if (!node) {
    return;
  }
  tree_dispose(node->left);
  tree_dispose(node->right);
  znode_del(container_of(node, ZNode, tree));
}

void zset_dispose(ZSet *zset) {
  tree_dispose(zset->tree);
  hm_destroy(&zset->hmap);
}
