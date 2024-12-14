#ifndef __KD_H
#define __KD_H
#include <stdlib.h>

// 3D KD Tree
typedef struct KDTree {
  float point[3];
  struct KDTree *left;
  struct KDTree *right;
} KDTree;

// Euclidian distance squared between two 3D coordinates
static inline float dist2(float *pt1, float *pt2){
  /*
    NOTE: Using the loop will yield incorrect, but almost identical code.
    It will use one more `vfmadd132ss`, instead of a `vmulss`, resulting
    in it counting one extra point pair.
  */
  /*
  float sum = 0;
  for (int i=0; i<3; i++){
    sum += (pt1[i]-pt2[i])*(pt1[i]-pt2[i]);
  }
  return sum;
  */
  return ((pt1[0]-pt2[0])*(pt1[0]-pt2[0])) + ((pt1[1]-pt2[1])*(pt1[1]-pt2[1])) +((pt1[2]-pt2[2])*(pt1[2]-pt2[2]));
}

KDTree *kd_create(float *pts, int len);
KDTree *kd_insert(struct KDTree *kdtree, float pt[3], int depth);
int kd_count_neighbours_traverse(float range2, float point[3], KDTree *kdtree, int axis);
void kd_free(struct KDTree *kdtree);
int kd_size(struct KDTree *kdtree);
int kd_max_depth(struct KDTree *kdtree);

#endif
