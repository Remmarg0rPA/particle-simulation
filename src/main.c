#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <smmintrin.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define PARSE_IMPLEMENTATION
#include "parse.h"

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

#define BBSIZE 0.1
#define NBB (int)((10-(-10))/BBSIZE+1 + 2)
#define INDEX(i,j,k) ((i+1)*NBB*NBB + (j+1)*NBB + (k+1))

#define CHUNK_SIZE 16
typedef struct LinkedList {
  float pt[4*CHUNK_SIZE];
  struct LinkedList *next;
  int used;
} LinkedList;

typedef struct parser_args {
  char *file_start;
  char *file_end;
} parser_args;

typedef struct counter_args {
  LinkedList **grid;
  int start_idx;
  int end_idx;
  int stride;
  volatile long npairs;
} counter_args;

typedef struct parser_state {
  int nthreads;
  pthread_t *threads;
  parser_args *threads_args;
  volatile LinkedList **grid;
  volatile atomic_int n_running;
  volatile atomic_int has_started;
} parser_state;

typedef struct counter_state {
  int nthreads;
  pthread_t *threads;
  counter_args *threads_args;
  volatile atomic_int n_running;
} counter_state;

parser_state pstate = {
  .nthreads = 4,
  .threads = NULL,
  .threads_args = NULL,
  .grid = NULL,
  .n_running = 0,
  .has_started = 0,
};

counter_state cstate = {
  .nthreads = 4,
  .threads = NULL,
  .threads_args = NULL,
  .n_running = 0,
};

void usage(char **argv){
  printf("Usage: %s -f <data file> [-p NUM] [-c NUM]\n", argv[0]);
  puts("\t-c Num\tNumber of threads to use while counting pairs. Default is 4.");
  puts("\t-p Num\tNumber of threads to use while parsing. Default is 4.");
  puts("\t-h\tPrint this message and exit.");
}

static inline float dist2(float *pt1, float *pt2){
  __m128 v1 = _mm_loadu_ps(pt1);
  __m128 v2 = _mm_loadu_ps(pt2);
  __m128 diff = v1-v2;
  __m128 dp = _mm_dp_ps(diff, diff, 0x71);
  return _mm_cvtss_f32(dp);
}

void join_threads(int nthreads, pthread_t *threads){
  for (int i=0; i<nthreads; i++){
    if (0 != pthread_join(threads[i], NULL)){
      perror("pthread_join");
      exit(-1);
    }
  }
}

/*
  Insert a new point into the grid using an atomic exchange operation.
*/
static inline void atomic_insert_new(float *pt, volatile LinkedList **grid){
  int x = (int)((pt[0] - (-10.))/BBSIZE);
  int y = (int)((pt[1] - (-10.))/BBSIZE);
  int z = (int)((pt[2] - (-10.))/BBSIZE);

  // Create a new chunk in the cell. More than one chunk might be created by
  // multiple threads trying to insert simultaneously. This does not affect
  // the end result, but some chunks might not be full.
  if (grid[INDEX(x,y,z)] == NULL){
    LinkedList *node = calloc(1, sizeof(LinkedList));
    if (node == NULL) {
      perror("calloc");
      exit(-1);
    }
    node->used = 1;
    // Move address of node into the grid and the current address there into node->next
    __atomic_exchange(&grid[INDEX(x,y,z)], (volatile LinkedList **)&node,
                      (volatile LinkedList **)&node->next, __ATOMIC_SEQ_CST);
    node->pt[0] = pt[0];
    node->pt[1] = pt[1];
    node->pt[2] = pt[2];
    return;
  }

  volatile LinkedList *cell = __atomic_load_n(&grid[INDEX(x,y,z)], __ATOMIC_SEQ_CST);
  int used = __atomic_add_fetch(&cell->used, 1, __ATOMIC_SEQ_CST);
  // Thread filling last slot is responsible for creating a new chunk
  if (used == CHUNK_SIZE){
    LinkedList *node = calloc(1, sizeof(LinkedList));
    if (node == NULL) {
      perror("calloc");
      exit(-1);
    }
    // Move address of node into the grid and the current address there into node->next
    __atomic_exchange(&grid[INDEX(x,y,z)], (volatile LinkedList **)&node,
                      (volatile LinkedList **)&node->next, __ATOMIC_SEQ_CST);
  } else if (used > CHUNK_SIZE){
    // Restore cell->used and loop back until a new chunk has been inserted
    cell->used = CHUNK_SIZE;
    atomic_insert_new(pt, grid);
    return;
  }
  cell->pt[(used-1)*4 + 0] = pt[0];
  cell->pt[(used-1)*4 + 1] = pt[1];
  cell->pt[(used-1)*4 + 2] = pt[2];
}

/*
  Parses 3 floats.
  NOTE: Assumes that the first char is the start of the float
 */
static inline char *parse_line(char *str, float *data) {
  char *end = NULL;
  #pragma GCC unroll 3
  for (int i=0; i<3; i++){
    *data = parse_float(str, &end);
    str = end+1;
    while (*str<=' ' && *str>'\0'){
      str++;
    }
    data++;
  }
  return str;
}

void *parser_thread(void *__pargs){
  pstate.n_running++;
  pstate.has_started++;
  parser_args *pargs = (parser_args *)__pargs;
  char *str = pargs->file_start;
  float data_buf[4];
  volatile LinkedList **grid = pstate.grid;
  while (str < pargs->file_end){
    str = parse_line(str, data_buf);
    atomic_insert_new(data_buf, grid);
  }
  pstate.n_running--;
  return NULL;
}

void start_parser(char *file, size_t size, LinkedList **grid){
  const int nthreads = pstate.nthreads;
  pstate.threads = malloc(nthreads*sizeof(pthread_t));
  pstate.threads_args = malloc(nthreads*sizeof(parser_args));
  if (pstate.threads == NULL || pstate.threads_args == NULL){
    perror("malloc");
    exit(-1);
  }
  pstate.grid = (volatile LinkedList **)grid;

  // Init args to threads
  pstate.threads_args[0].file_start = file;
  for (int i=1; i<nthreads; i++){
    pstate.threads_args[i].file_start = file + (size/nthreads)*i;
    // Adjust the chunks starting points until they reach a newline
    // and set it as the end for previous thread.
    while (*pstate.threads_args[i].file_start != '\n') {
      pstate.threads_args[i].file_start++;
    }
    // Set end of previous to just before start of next
    pstate.threads_args[i-1].file_end = pstate.threads_args[i].file_start;
    pstate.threads_args[i].file_start++;
  }
  pstate.threads_args[nthreads-1].file_end = file + size;

  for (int i=0; i<nthreads; i++){
    if (0 != pthread_create(&pstate.threads[i], NULL, &parser_thread, &pstate.threads_args[i])){
      perror("pthread_create");
      exit(-1);
    }
  }
}

int count_chunk(float pt[4], LinkedList *chunk){
  int count = 0;
  int used = chunk->used;
  for (int i=0; i<used; i++){
    if (dist2(pt, &chunk->pt[i*4])<=0.0025){
      count++;
    }
  }
  return count;
}

int compare(float *pt, LinkedList *head){
  int count = 0;
  while (head != NULL){
    count += count_chunk(pt, head);
    head = head->next;
  }
  return count;
}

void *counter_thread(void *__cargs){
  counter_args *cargs = (counter_args *)__cargs;
  LinkedList **grid = cargs->grid;
  int sidx = cargs->start_idx;
  int eidx = cargs->end_idx;
  int stride = cargs->stride;
  long npairs = 0;

  for (int i=sidx; i<eidx; i+=stride){
    for (int j=0; j<NBB; j+=1){
      for (int k=0; k<NBB; k+=1){
        LinkedList *head = grid[INDEX(i,j,k)];
        while (head!=NULL){
          int used = head->used;
          for (int n=0; n<used; n++){
            float *cur = &head->pt[n*4];
            // Compare point with points after it in current chunk
            for (int m=n+1; m<used; m++){
              if (dist2(cur, &head->pt[m*4])<=0.0025){
                npairs++;
              }
            }
            // Compare with following chunks in the same cell
            npairs += compare(cur, head->next);

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
          head = head->next;
        }
      }
    }
  }
  cargs->npairs = npairs;
  return NULL;
}

long count(LinkedList **grid){
  const int nthreads = cstate.nthreads;
  cstate.threads = malloc(nthreads*sizeof(pthread_t));
  cstate.threads_args = malloc(nthreads*sizeof(counter_args));
  for (int i=0; i<nthreads; i++){
    cstate.threads_args[i].grid = grid;
    cstate.threads_args[i].start_idx = i;
    cstate.threads_args[i].end_idx = NBB;
    cstate.threads_args[i].stride = nthreads;
  }
  // Create threads
  for (int i=0; i<nthreads; i++){
    if (0 != pthread_create(&cstate.threads[i], NULL, &counter_thread, &cstate.threads_args[i])){
      perror("pthread_create");
      exit(-1);
    }
  }
  long npairs = 0;
  // Join threads
  for (int i=0; i<nthreads; i++){
    if (0 != pthread_join(cstate.threads[i], NULL)){
      perror("pthread_join");
      exit(-1);
    }
    npairs += cstate.threads_args[i].npairs;
  }
  free(cstate.threads);
  free(cstate.threads_args);
  return npairs;
}

int main(int argc, char **argv){
  START_TIMER();
  char *file_name = NULL;
  int i = 1;
  while (i < argc) {
    switch (argv[i][0]){
    case '-':
      switch (argv[i][1]){
      case 'c':
        i++;
        if (0 >= (cstate.nthreads = atoi(argv[i]))){
          perror("atoi");
          return -1;
        }
        break;
      case 'f':
        i++;
        file_name = argv[i];
        break;
      case 'h':
        usage(argv);
        return 0;
      case 'p':
        i++;
        if (0 >= (pstate.nthreads = atoi(argv[i]))){
          perror("atoi");
          return -1;
        }
        break;
      default:
        printf("Unrecognized option \"-%s\"\n", &argv[i][1]);
        usage(argv);
        return -1;
      }
      break;
    default:
      printf("Unrecognized option \"%s\"\n", argv[i]);
      usage(argv);
      return -1;
    }
    i++;
  }

  int fd = open(file_name, O_RDONLY);
  if (fd == -1){
    perror("open");
    return -1;
  }

  // Get file size
  struct stat sb;
  if (fstat(fd, &sb) == -1) {
    perror("fstat");
    return -1;
  }
  long size = sb.st_size;

  char *file = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
  if (file == MAP_FAILED){
    perror("mmap");
    return -1;
  }

  #define GRID_SIZE (NBB+1)*(NBB+1)*(NBB+1)
  LinkedList **grid = calloc(GRID_SIZE, sizeof(LinkedList *));
  if (grid == NULL){
    perror("calloc");
    return -1;
  }
  STOP_TIMER("init");

  START_TIMER();
  start_parser(file, size, grid);
  join_threads(pstate.nthreads, pstate.threads);
  STOP_TIMER("parsing");

  START_TIMER();
  long npairs = count(grid);
  STOP_TIMER("count");

  printf("%ld\n", npairs);

  START_TIMER();
  close(fd);
  munmap(file, size);
  free(grid);
  STOP_TIMER("cleanup");
  return 0;
}
