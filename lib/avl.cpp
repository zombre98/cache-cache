#include <cstddef>
#include <cstdint>
#include <stddef.h>
#include <stdint.h>
#include <sys/wait.h>
#include "avl.h"

static uint32_t avl_depth(AVLNode *node) {
  return node ? node->depth : 0;
}

static uint32_t avl_cnt(AVLNode *node) {
  return node ? node->cnt : 0;
}

static uint32_t max(uint32_t left, uint32_t right) {
  return left > right ? left : right;
}

static void avl_update(AVLNode *node) {
  node->depth = max(avl_depth(node->left), avl_depth(node->right)) + 1;
  node->cnt = avl_cnt(node->left) + avl_cnt(node->right) + 1;
}

static AVLNode *left_rotation(AVLNode *node) {
  AVLNode *new_node = node->right;
  if (new_node->left) {
    new_node->left->parent = node;
  }
  node->right = new_node->left;
  new_node->left = node;
  new_node->parent = node->parent;
  node->parent = new_node;
  avl_update(node);
  avl_update(new_node);
  return new_node;
}

static AVLNode *right_rotation(AVLNode *node) {
  AVLNode *new_node = node->left;
  if (new_node->right) {
    new_node->right->parent = node;
  }
  node->left = new_node->right;
  new_node->right = node;
  new_node->parent = node->parent;
  node->parent = new_node;
  avl_update(node);
  avl_update(new_node);
  return new_node;
}

static AVLNode *avl_fix_left(AVLNode *root) {
  if (avl_depth(root->left->left) < avl_depth(root->left->right)) {
    // A left rotation makes the left subtree deeper than the right subtree while keeping the rotated height if the right subtree is deeper by 1.
    root->left = left_rotation(root->left);
  }
  // A right rotation restores balance if the left subtree is deeper by 2, and the left left subtree is deeper than the left right subtree.
  return right_rotation(root);
}

static AVLNode *avl_fix_right(AVLNode *root) {
  if (avl_depth(root->right->right) < avl_depth(root->right->left)) {
    root->right = right_rotation(root->right);
  }
  return left_rotation(root);
}

/*
 * This function fixes imbalances after an insertion or a deletion. Maintain invariants until root is reached.
 */
AVLNode *avl_fix(AVLNode *node) {
  while (true) {
    avl_update(node);
    uint32_t l = avl_depth(node->left);
    uint32_t r = avl_depth(node->right);
    AVLNode **from = NULL;
    if (AVLNode *p = node->parent) {
      from = (p->left == node) ? &p->left : &p->right;
    }
    if (l == r + 2) {
      node = avl_fix_left(node);
    } else if(l + 2 == r) {
      node = avl_fix_right(node);
    }
    if (!from) {
      return node;
    }
    *from = node;
    node = node->parent;
   }
}

/*
 * Detach a node and returns the new root tree
 * No right subtree, replace the node with left subtree and link the left subtree to the parent
 */
AVLNode *avl_del(AVLNode *node) {
  if (node->right == NULL) {
    AVLNode *parent = node->parent;
    if (node->left) {
      node->left->parent = parent;
    }
    if (parent) {
      (parent->left == node ? parent->left : parent->right) = node->left;
      return avl_fix(parent);
    } else {
      return node->left;
    }
  } else {
    // Detach the successor
    AVLNode *victim = node->right;
    while (victim->left) {
      victim = victim->left;
    }
    AVLNode *root = avl_del(victim);
    *victim = *node;
    if (victim->left) {
      victim->left->parent = victim;
    }
    if (victim->right) {
      victim->right->parent = victim;
    }
    if (AVLNode *parent = node->parent) {
      (parent->left == node ? parent->left : parent->right) = victim;
      return root;
    } else {
      return victim;
    }
  }
}

// offset into succeding or preceding node.
// note: the worst-case is O(log(n)) regarless of how long the offset is.
AVLNode *avl_offset(AVLNode *node, int64_t offset) {
  int64_t pos = 0;
  while (offset != pos) {
    if (pos < offset && pos + avl_cnt(node->right) >= offset) {
      // target in right subtree
      node = node->right;
      pos += avl_cnt(node->left) + 1;
    } else if (pos > offset && pos - avl_cnt(node->left) >= offset) {
      // target in left subtree
      node = node->left;
      pos -= avl_cnt(node->right) + 1;
    } else {
      // go parent
      AVLNode *parent = node->parent;
      if (!parent) {
        return NULL;
      }
      if (parent->right == node) {
        pos -= avl_cnt(node->left) + 1;
      } else {
        pos += avl_cnt(node->right) +1;
      }
      node = parent;
    }
  }
  return node;
}
