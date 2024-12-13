#include <stdlib.h>
#include <stdio.h>

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

  long count = 0;
  for (long i=used-3; i>=0; i-=3){
    for (long j=i-3; j>=0; j-=3){
      float dx = data[j] - data[i];
      float dy = data[j+1] - data[i+1];
      float dz = data[j+2] - data[i+2];
      if (dx * dx + dy * dy + dz * dz <= 0.05 * 0.05) {
        count += 1;
      }
    }
  }
  printf("Count = %ld\n", count);

  free(data);
  return 0;
}
