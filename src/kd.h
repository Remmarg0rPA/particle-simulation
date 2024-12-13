#ifndef __KD_H
#define __KD_H
#include <stdlib.h>

// 3D KD Tree
typedef struct KDTree {
  float point[3];
  struct KDTree *left;
  struct KDTree *right;
} KDTree;

// Euclidian distance squared between 2 points
#define DIST2(xs, ys) (((xs)[0]-(ys)[0])*((xs)[0]-(ys)[0])) + (((xs)[1]-(ys)[1])*((xs)[1]-(ys)[1])) +(((xs)[2]-(ys)[2])*((xs)[2]-(ys)[2]))

KDTree *kd_create(float *pts, int len);
KDTree *kd_insert(struct KDTree *kdtree, float pt[3], int depth);
int kd_count_neighbours_traverse(float range, float point[3], KDTree *kdtree, int depth);
void kd_free(struct KDTree *kdtree);
int kd_size(struct KDTree *kdtree);

#endif
