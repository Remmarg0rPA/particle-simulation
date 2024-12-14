#include "kd.h"

/*
  Create a KD tree by inserting each point into an initially empty KD tree
  without sorting, creating an unbalanced KD tree.
 */
KDTree *kd_create(float *pts, int len){
  KDTree *kdtree = NULL;
  for (int i=len-3; i >= 0; i-=3){
    kdtree = kd_insert(kdtree, &pts[i], 0);
  }
  return kdtree;
}

/*
  Insert a point into a KD tree without rebalancing
 */
KDTree *kd_insert(struct KDTree *root, float pt[3], int axis){
  // The node to be appended to the tree
  KDTree *leaf = malloc(sizeof(KDTree));
  leaf->point[0] = pt[0];
  leaf->point[1] = pt[1];
  leaf->point[2] = pt[2];
  leaf->left = NULL;
  leaf->right = NULL;
  if (root == NULL){
    return leaf;
  }
  
  KDTree *kd = root;
  while (1){
    if (pt[axis] < kd->point[axis]){
      if (kd->left != NULL){
        kd = kd->left;
      } else {
        kd->left = leaf;
        return root;
      }
    } else{
      if (kd->right != NULL){
        kd = kd->right;
      } else {
        kd->right = leaf;
        return root;
      }
    }
    axis = (axis+1)%3;
  }
}

/*
  Free a KD tree from memory
 */
void kd_free(struct KDTree *kdtree){
  if (kdtree != NULL){
    KDTree *left = kdtree->left;
    KDTree *right = kdtree->right;
    free(kdtree);
    kd_free(left);
    kd_free(right);
  }
}

/*
  Counts the number of nodes in a KD tree
 */
int kd_size(struct KDTree *kdtree){
  int count = 0;
  if (kdtree != NULL){
    count += 1;
    count += kd_size(kdtree->left);
    count += kd_size(kdtree->right);
  }
  return count;
}

/*
  Counts the max depth of a KD tree
 */
int kd_max_depth(struct KDTree *kdtree){
  if (kdtree == NULL){
    return 0;
  } else {
    int d1 = kd_max_depth(kdtree->left);
    int d2 = kd_max_depth(kdtree->right);
    return 1 + (d1 > d2 ? d1 : d2);
  }
}

/*
  Checks for close neighbours to a given point in the whole tree,
  pruning subtrees out of range.
  NOTE: Will always count a point paired with itself as within
  the range and will count each pair of distinct points twice.
 */
int kd_count_neighbours_traverse(float range2, float point[3], KDTree *kdtree, int axis){
  int count = 0;
  axis = axis %3;
  if (kdtree != NULL) {
    // Check distance to current point
    count += dist2(point, kdtree->point)<=range2 ? 1 : 0;

    // Could exist close points in both subtrees
    if ((point[axis]-kdtree->point[axis])*(point[axis]-kdtree->point[axis]) <= range2){
      count += kd_count_neighbours_traverse(range2, point, kdtree->right, axis+1);
      count += kd_count_neighbours_traverse(range2, point, kdtree->left, axis+1);
      // Prune right subtree
    } else if (point[axis] <= kdtree->point[axis]) {
      count += kd_count_neighbours_traverse(range2, point, kdtree->left, axis+1);
      // Prune left subtree
    } else {
      count += kd_count_neighbours_traverse(range2, point, kdtree->right, axis+1);
    }
  }
  return count;
}
