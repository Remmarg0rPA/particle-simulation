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

void kd_free(struct KDTree *kdtree){
  if (kdtree != NULL){
    KDTree *left = kdtree->left;
    KDTree *right = kdtree->right;
    free(kdtree);
    kd_free(left);
    kd_free(right);
  }
}

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
Find nearest neighbour of given point.
 */
KDTree *kd_nearest_neighbour(KDTree *kdtree, float pt[3], int depth, KDTree *best, float best_dist2){
  if (kdtree == NULL) {
    return NULL;
  }
  int axis = depth % 3;

  // 1D distance to point
  float dist2 = (kdtree->point[axis]-pt[axis])*(kdtree->point[axis]-pt[axis]);
  if (dist2 >= best_dist2){
    return NULL;
  }

  // Euclidian distance squared between the points
  dist2 = DIST2(pt, kdtree->point);
  if (dist2 < best_dist2){
    best = kdtree;
    best_dist2 = dist2;
  }
  // Check recursively
  if (pt[axis] <= kdtree->point[axis]){
    kd_nearest_neighbour(kdtree->left, pt, axis+1, best, best_dist2);
    kd_nearest_neighbour(kdtree->right, pt, axis+1, best, best_dist2);
  } else {
    kd_nearest_neighbour(kdtree->right, pt, axis+1, best, best_dist2);
    kd_nearest_neighbour(kdtree->left, pt, axis+1, best, best_dist2);
  }
  return best;
}

/*
  Checks for close neighbours to a given point in the whole tree,
  pruning subtrees out of range.
  NOTE: Will always count a point paired with itself as within
  the range and will count each pair of distinct points twice.
 */
int kd_count_neighbours_traverse(float range2, float point[3], KDTree *kdtree, int depth){
  int count = 0;
  int axis = depth % 3;
  if (kdtree != NULL) {
    // Check distance to current point
    count += DIST2(point, kdtree->point)<=range2 ? 1 : 0;

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
