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
  volatile float *data_next_empty;
  float *data_end;
} parser_args;

void *parser_thread(void *__pargs){
  parser_args *pargs = (parser_args *)__pargs;
  FILE *fp = pargs->fp;
  pthread_mutex_t *mutex = pargs->mutex;
  float *data_next = (float *)pargs->data_next_empty;
  float *data_end = pargs->data_end;

  size_t buf_size = 0x100;
  char *buf = malloc(buf_size);
  float *data_ptr;
  while (1){
    pthread_mutex_lock(mutex);
    if (0 >= getline(&buf, &buf_size, fp)){
      pthread_mutex_unlock(mutex);
      break;
    }
    data_ptr = (float *)pargs->data_next_empty;
    pargs->data_next_empty += 3;
    pthread_mutex_unlock(mutex);
    parse_line(buf, data_ptr);
  }
  free(buf);
  if (data_next >= data_end){
    perror("parser overflow");
    exit(-1);
  }
  return NULL;
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
  parser_args pargs = {
    .fp = fp,
    .mutex = &mutex,
    .data = data,
    .data_next_empty = data,
    .data_end = data+size
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
  return pargs.data_next_empty - pargs.data;
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

typedef struct counter_args {
  LinkedList **grid;
  int start_idx;
  int end_idx;
  int stride;
  volatile long npairs;
} counter_args;

/*
  NOTE: Only valid for counting internal cells and not the boundary cells.
*/
void *counter_thread(void *__cargs){
  counter_args *cargs = (counter_args *)__cargs;
  LinkedList **grid = cargs->grid;
  int sidx = cargs->start_idx;
  int eidx = cargs->end_idx;
  int stride = cargs->stride;
  long npairs = 0;
  
  for (int i=sidx; i<eidx; i+=stride){
    for (int j=1; j<NBB-1; j+=1){
      for (int k=1; k<NBB-1; k+=1){
        LinkedList *head = grid[INDEX(i,j,k)];
        while (head!=NULL){
          float *cur = head->pt;
          head = head->next;
          npairs += compare(cur, head);

          // Check neighbouring cells
          npairs += compare(cur, grid[INDEX(i+1, j, k)]);
          npairs += compare(cur, grid[INDEX(i+1, j+1, k)]);
          npairs += compare(cur, grid[INDEX(i+1, j+1, k+1)]);
          npairs += compare(cur, grid[INDEX(i+1, j+1, k-1)]);
          npairs += compare(cur, grid[INDEX(i+1, j-1, k)]);
          npairs += compare(cur, grid[INDEX(i+1, j-1, k+1)]);
          npairs += compare(cur, grid[INDEX(i+1, j-1, k-1)]);
          npairs += compare(cur, grid[INDEX(i+1, j, k-1)]);
          npairs += compare(cur, grid[INDEX(i+1, j, k+1)]);
          npairs += compare(cur, grid[INDEX(i, j+1, k)]);
          npairs += compare(cur, grid[INDEX(i, j+1, k+1)]);
          npairs += compare(cur, grid[INDEX(i, j+1, k-1)]);
          npairs += compare(cur, grid[INDEX(i, j, k+1)]);
        }
      }
    }
  }
  cargs->npairs = npairs;
  return NULL;
}

void *count_boundary(void *__cargs){
  counter_args *cargs = (counter_args *)__cargs;
  LinkedList **grid = cargs->grid;
  long npairs = 0;
    // Check boundaries
  for (int i=0; i<NBB; i+=NBB-1){
    for (int j=0; j<NBB; j+=NBB-1){
      for (int k=0; k<NBB; k+=NBB-1){
        LinkedList *head = grid[INDEX(i,j,k)];
        while (head!=NULL){
          float *cur = head->pt;
          head = head->next;
          npairs += compare(cur, head);

          // Check neighbouring cells
          if (i != NBB-1){
            npairs += compare(cur, grid[INDEX(i+1, j, k)]);
            if (j != NBB-1){
              npairs += compare(cur, grid[INDEX(i+1, j+1, k)]);
              if (k != NBB-1)
                npairs += compare(cur, grid[INDEX(i+1, j+1, k+1)]);
              if (k != 0)
                npairs += compare(cur, grid[INDEX(i+1, j+1, k-1)]);
            }

            if (j != 0){
              npairs += compare(cur, grid[INDEX(i+1, j-1, k)]);
              if (k != NBB-1)
                npairs += compare(cur, grid[INDEX(i+1, j-1, k+1)]);
              if (k != 0)
                npairs += compare(cur, grid[INDEX(i+1, j-1, k-1)]);
            }

            if ( k != 0)
              npairs += compare(cur, grid[INDEX(i+1, j, k-1)]);
            if (k != NBB-1)
              npairs += compare(cur, grid[INDEX(i+1, j, k+1)]);
          }

          if (j != NBB-1){
            npairs += compare(cur, grid[INDEX(i, j+1, k)]);
            if (k != NBB-1)
              npairs += compare(cur, grid[INDEX(i, j+1, k+1)]);
            if (k != 0)
              npairs += compare(cur, grid[INDEX(i, j+1, k-1)]);
          }

          if (k != NBB-1)
            npairs += compare(cur, grid[INDEX(i, j, k+1)]);
        }
      }
    }
  }
  cargs->npairs = npairs;
  return NULL;
}

long count(LinkedList **grid){
  const int nthreads = 4;
  pthread_t *threads = malloc(nthreads*sizeof(pthread_t));
  counter_args *cargs = malloc(nthreads*sizeof(counter_args));
  for (int i=0; i<nthreads; i++){
    cargs[i].grid = grid;
    cargs[i].start_idx = i+1;
    cargs[i].end_idx = NBB-1;
    cargs[i].stride = nthreads;
  }
  // Create threads
  for (int i=0; i<nthreads; i++){
    if (0 != pthread_create(&threads[i], NULL, &counter_thread, &cargs[i])){
      perror("pthread_create");
      exit(-1);
    }
  }
  counter_args boundary_args = {
    .grid = grid,
  };
  count_boundary((void *)&boundary_args);
  long npairs = boundary_args.npairs;

  // Join threads
  for (int i=0; i<nthreads; i++){
    if (0 != pthread_join(threads[i], NULL)){
      perror("pthread_join");
      exit(-1);
    }
    npairs += cargs[i].npairs;
  }
  return npairs;
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
  long npairs = count(grid);
  STOP_TIMER("count")

  printf("%ld\n", npairs);
  free(data);
  return 0;
}
