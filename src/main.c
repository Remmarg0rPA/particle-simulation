#include <stdlib.h>
#include <stdio.h>
#include <omp.h>
#include <stdatomic.h>
#include "kd.h"

/* #define TIMER */
#ifdef TIMER
#include <sys/time.h>
struct timeval stop, start;
#define START_TIMER() gettimeofday(&start, NULL);
#define STOP_TIMER(name) gettimeofday(&stop, NULL);printf("%16s:  %10lu us\n", name, (stop.tv_sec-start.tv_sec)*1000000 + stop.tv_usec-start.tv_usec);

#else
#define START_TIMER()
#define STOP_TIMER(name)

#endif

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

  START_TIMER()
  // Get file size
  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  STOP_TIMER("filesize")

  START_TIMER()
  // This will be a small overestimation of needed space for positions.xyz,
  // and a large for positions_large.xyz
  float *data = malloc(size);
  STOP_TIMER("malloc")

  START_TIMER()
  // NOTE: does not properly check for fail in fscanf or overflow of data
  long used = 0;
  while (3 == fscanf(fp, "%f %f %f", &data[used], &data[used+1], &data[used+2])){
    used += 3;
  }
  fclose(fp);
  STOP_TIMER("parsing")

  START_TIMER()
  KDTree *kd = kd_create(data, used);
  STOP_TIMER("KD create")

  START_TIMER()
  atomic_long count = 0;
  #pragma omp parallel for
  for (long i=used-3; i>=0; i-=3){
    count += kd_count_neighbours_traverse(0.05*0.05, &data[i], kd, 0);
  }
  count -= used/3;
  count /= 2;
  STOP_TIMER("count")

  printf("Count = %ld\n", count);
  free(data);
  return 0;
}
