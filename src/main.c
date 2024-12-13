#include <stdlib.h>
#include <stdio.h>
#include "kd.h"

int main(int argc, char **argv){
  if (argc < 2){
    printf("Usage: %s <data file>", argv[0]);
    return -1;
  }

  FILE *fp = fopen(argv[1], "r");
  if (fp == NULL){
    perror("fopen");
    return -1;
  }

  // Get file size
  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  // This will be a small overestimation of needed space for positions.xyz,
  // and a large for positions_large.xyz
  float *data = malloc(size);

  // NOTE: does not properly check for fail in fscanf or overflow of data
  long used = 0;
  while (3 == fscanf(fp, "%f %f %f", &data[used], &data[used+1], &data[used+2])){
    used += 3;
  }
  fclose(fp);

  KDTree *kd = kd_create(data, used);
  long count = 0;
  for (long i=used-3; i>=0; i-=3){
    count += kd_count_neighbours_traverse(0.05*0.05, &data[i], kd, 0);
  }
  count -= used/3;
  count /= 2;
  printf("Count = %ld\n", count);
  free(data);
  return 0;
}
