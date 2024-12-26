#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <omp.h>
#include <stdatomic.h>
#include <pthread.h>
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

/*
  Parses 3 floats.
  NOTE: Assumes that the first char is the start of the float
  and that they are separated by only 1 whitespace.
 */
static inline void parse_line(char *str, float *data) {
  char *end = NULL;
  #pragma GCC unroll 3
  for (int i=0; i<3; i++){
    *data = strtof(str, &end);
    str = end+1;
    data++;
  }
}

typedef struct parser_args {
  FILE *fp;
  pthread_mutex_t *mutex;
  float *data;
  volatile size_t *data_used;
  size_t data_size;
} parser_args;

void *parser_thread(void *pargs){
  FILE *fp = ((parser_args *)pargs)->fp;
  float *data = ((parser_args *)pargs)->data;
  pthread_mutex_t *mutex = ((parser_args *)pargs)->mutex;
  volatile size_t *n_read_floats = ((parser_args *)pargs)->data_used;
  size_t data_size = ((parser_args *)pargs)->data_size;

  size_t buf_size = 0x100;
  char *buf = malloc(buf_size);
  float *data_ptr;
  ssize_t n;
  while (1){
    pthread_mutex_lock(mutex);
    if (0 >= (n=getline(&buf, &buf_size, fp))){
      pthread_mutex_unlock(mutex);
      if (sizeof(float)*(*n_read_floats) >= data_size){
        perror("parser overflow");
        exit(-1);
      }
      return NULL;
    }
    data_ptr = data + *n_read_floats;
    *n_read_floats += 3;
    pthread_mutex_unlock(mutex);
    parse_line(buf, data_ptr);
  }
}


/*
  Parse floats into the data buffer with size bytes from fp.
  Returns number of floats read.
 */
size_t parse(FILE *fp, float *data, size_t size){
  // Mutex used for reading from fp and read/write index `used`
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  if (pthread_mutex_init(&mutex, NULL) != 0) {
    perror("mutex");
    exit(-1);
  }

  const int nthreads = 6;
  pthread_t *threads = malloc(nthreads*sizeof(pthread_t));
  volatile size_t used = 0;
  parser_args pargs = {
    .fp = fp,
    .mutex = &mutex,
    .data = data,
    .data_used = &used,
    .data_size = size
  };

  // Create threads
  for (int i=0; i<nthreads; i++){
    if (0 != pthread_create(&threads[i], NULL, &parser_thread, &pargs)){
      perror("pthread_create");
      exit(-1);
    }
  }

  // Join threads
  for (int i=0; i<nthreads; i++){
    if (0 != pthread_join(threads[i], NULL)){
      perror("pthread_join");
      exit(-1);
    }
  }
  return used;
}

#define BBSIZE 0.1
#define NBB (int)((10-(-10))/BBSIZE+1 + 2)
#define INDEX(i,j,k) ((i)*NBB*NBB + (j)*NBB + (k))

typedef struct LinkedList {
  float *pt;
  struct LinkedList *next;
} LinkedList;

static inline LinkedList *insert_new(float *pt, LinkedList *head){
  LinkedList *node = malloc(sizeof(LinkedList));
  if (node == NULL){
    perror("malloc");
    exit(-1);
  }
  node->pt = pt;
  node->next = head;
  return node;
}

void insert_into_grid(LinkedList **grid, float *data, int used){
  for (long i=0; i<used; i+=3){
    float *pt = &data[i];
    // All given floats are between ±10, therefore no bounding checks are performed
    int x = (int)((pt[0] - (-10.))/BBSIZE);
    int y = (int)((pt[1] - (-10.))/BBSIZE);
    int z = (int)((pt[2] - (-10.))/BBSIZE);
    grid[INDEX(x,y,z)] = insert_new(pt, grid[INDEX(x,y,z)]);
  }
}

int compare(float *pt, LinkedList *head){
  int count = 0;
  while (head != NULL){
    if (dist2(pt, head->pt) <= 0.05*0.05){
      count += 1;
    }
    head = head->next;
  }
  return count;
}

long do_count(LinkedList **grid){
  atomic_long count = 0;
  // Check neighbouring internal cells. Boundary cells are checked below
  #pragma omp parallel for
  for (int i=1; i<NBB-1; i++){
    for (int j=1; j<NBB-1; j++){
      for (int k=1; k<NBB-1; k++){
        LinkedList *head = grid[INDEX(i,j,k)];
        while (head!=NULL){
          float *cur = head->pt;
          head = head->next;
          count += compare(cur, head);

          // Check neighbouring cells
          count += compare(cur, grid[INDEX(i+1, j, k)]);
          count += compare(cur, grid[INDEX(i+1, j+1, k)]);
          count += compare(cur, grid[INDEX(i+1, j+1, k+1)]);
          count += compare(cur, grid[INDEX(i+1, j+1, k-1)]);
          count += compare(cur, grid[INDEX(i+1, j-1, k)]);
          count += compare(cur, grid[INDEX(i+1, j-1, k+1)]);
          count += compare(cur, grid[INDEX(i+1, j-1, k-1)]);
          count += compare(cur, grid[INDEX(i+1, j, k-1)]);
          count += compare(cur, grid[INDEX(i+1, j, k+1)]);
          count += compare(cur, grid[INDEX(i, j+1, k)]);
          count += compare(cur, grid[INDEX(i, j+1, k+1)]);
          count += compare(cur, grid[INDEX(i, j+1, k-1)]);
          count += compare(cur, grid[INDEX(i, j, k+1)]);
        }
      }
    }
  }

  // Check boundaries
  for (int i=0; i<NBB; i+=NBB-1){
    for (int j=0; j<NBB; j+=NBB-1){
      for (int k=0; k<NBB; k+=NBB-1){
        LinkedList *head = grid[INDEX(i,j,k)];
        while (head!=NULL){
          float *cur = head->pt;
          head = head->next;
          count += compare(cur, head);

          // Check neighbouring cells
          if (i != NBB-1){
            count += compare(cur, grid[INDEX(i+1, j, k)]);
            if (j != NBB-1){
              count += compare(cur, grid[INDEX(i+1, j+1, k)]);
              if (k != NBB-1)
                count += compare(cur, grid[INDEX(i+1, j+1, k+1)]);
              if (k != 0)
                count += compare(cur, grid[INDEX(i+1, j+1, k-1)]);
            }

            if (j != 0){
              count += compare(cur, grid[INDEX(i+1, j-1, k)]);
              if (k != NBB-1)
                count += compare(cur, grid[INDEX(i+1, j-1, k+1)]);
              if (k != 0)
                count += compare(cur, grid[INDEX(i+1, j-1, k-1)]);
            }

            if ( k != 0)
              count += compare(cur, grid[INDEX(i+1, j, k-1)]);
            if (k != NBB-1)
              count += compare(cur, grid[INDEX(i+1, j, k+1)]);
          }

          if (j != NBB-1){
            count += compare(cur, grid[INDEX(i, j+1, k)]);
            if (k != NBB-1)
              count += compare(cur, grid[INDEX(i, j+1, k+1)]);
            if (k != 0)
              count += compare(cur, grid[INDEX(i, j+1, k-1)]);
          }

          if (k != NBB-1)
            count += compare(cur, grid[INDEX(i, j, k+1)]);
        }
      }
    }
  }
  return count;
}

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
  if (data == NULL){
    perror("malloc");
    return -1;
  }
  STOP_TIMER("malloc")

  START_TIMER()
  // NOTE: does not check for overflow of data
  long used = parse(fp, data, size);
  fclose(fp);
  STOP_TIMER("parsing")

  START_TIMER()
  LinkedList **grid = calloc(NBB*NBB*NBB, sizeof(LinkedList *));
  if (grid == NULL){
    perror("calloc");
    return -1;
  }
  STOP_TIMER("calloc")

  START_TIMER()
  insert_into_grid(grid, data, used);
  STOP_TIMER("insertion")

  START_TIMER()
  long count = do_count(grid);
  STOP_TIMER("count")

  printf("%ld\n", count);
  free(data);
  return 0;
}
