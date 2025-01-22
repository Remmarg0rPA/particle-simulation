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
#define INDEX(i,j,k) ((i)*NBB*NBB + (j)*NBB + (k))

#define MIN(x,y) ((x)<(y)?(x):(y))

#define CHUNK_SIZE 16
typedef struct LinkedList {
  float pt[4*CHUNK_SIZE];
  struct LinkedList *next;
  int used;
} LinkedList;

typedef struct parser_state {
  int nthreads;
  pthread_t *threads;
  volatile LinkedList **grid;
  char *file;
  size_t file_size;
  volatile atomic_int n_running;
  volatile int has_started;
} parser_state;

typedef struct counter_state {
  int nthreads;
  pthread_t *threads;
  LinkedList **grid;
  volatile int next_index;
  int end_idx;
  int chunk_size;
  atomic_long npairs;
  volatile atomic_int n_running;
  volatile int has_started;
} counter_state;

parser_state pstate = {
  .nthreads = 4,
  .threads = NULL,
  .grid = NULL,
  .file = NULL,
  .file_size = 0,
  .n_running = 0,
  .has_started = 0,
};

counter_state cstate = {
  .nthreads = 4,
  .threads = NULL,
  .grid = NULL,
  .next_index = INDEX(1,1,1),
  .end_idx = INDEX(NBB-1, NBB-1, NBB-1),
  .chunk_size = 512,
  .npairs = 0,
  .n_running = 0,
  .has_started = 0,
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
                      (volatile LinkedList **)&node->next, __ATOMIC_RELEASE);
    node->pt[0] = pt[0];
    node->pt[1] = pt[1];
    node->pt[2] = pt[2];
    return;
  }

  volatile LinkedList *cell = __atomic_load_n(&grid[INDEX(x,y,z)], __ATOMIC_RELAXED);
  int used = __atomic_add_fetch(&cell->used, 1, __ATOMIC_RELAXED);
  // Thread filling last slot is responsible for creating a new chunk
  if (used == CHUNK_SIZE){
    LinkedList *node = calloc(1, sizeof(LinkedList));
    if (node == NULL) {
      perror("calloc");
      exit(-1);
    }
    // Move address of node into the grid and the current address there into node->next
    __atomic_exchange(&grid[INDEX(x,y,z)], (volatile LinkedList **)&node,
                      (volatile LinkedList **)&node->next, __ATOMIC_RELAXED);
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
    data++;
  }
  while (*str<' ' && *str>'\0'){
    str++;
  }
  return str;
}

void *parser_thread(){
  // Increment the active threads count
  int ptid = __atomic_fetch_add(&pstate.has_started, 1, __ATOMIC_SEQ_CST);
  pstate.n_running++;
  int nthreads = pstate.nthreads;
  char *file_start = pstate.file;
  size_t file_size = pstate.file_size;
  size_t chunk_size = file_size/nthreads;
  // Calculate start of section to parse
  char *str = file_start + ptid*chunk_size;
  // Advance start to beginning of a float
  if (ptid != 0){
    while (*str > '\n'){
      str++;
    }
    str++;
  }
  // Calculate end of section to parse
  char *file_end;
  if (ptid == nthreads-1){
    file_end = file_start + file_size;
  } else {
    file_end = file_start + (ptid+1)*chunk_size;
    // Make sure that it ends with a new line
    while (*file_end > '\n'){
      file_end++;
    }
  }
  float data_buf[4];
  volatile LinkedList **grid = pstate.grid;
  while (str < file_end){
    str = parse_line(str, data_buf);
    atomic_insert_new(data_buf, grid);
  }
  pstate.n_running--;
  return NULL;
}

void start_parser(char *file, size_t size, LinkedList **grid){
  const int nthreads = pstate.nthreads;
  pstate.threads = malloc(nthreads*sizeof(pthread_t));
  if (pstate.threads == NULL){
    perror("malloc");
    exit(-1);
  }
  pstate.grid = (volatile LinkedList **)grid;
  pstate.file = file;
  pstate.file_size = size;
  for (int i=0; i<nthreads; i++){
    if (0 != pthread_create(&pstate.threads[i], NULL, &parser_thread, NULL)){
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

void *counter_thread(){
  cstate.n_running++;
  cstate.has_started++;
  LinkedList **grid = cstate.grid;
  int chunk_size = cstate.chunk_size;
  volatile int *next_idx_ptr = &cstate.next_index;
  int end_idx = cstate.end_idx;
  int sidx, eidx;
  long npairs = 0;

  // Fetch an interval of chunk_size and iterate through the grid entries
  while ((sidx = __atomic_fetch_add(next_idx_ptr, chunk_size, __ATOMIC_RELAXED))){
    eidx = MIN(sidx+chunk_size, end_idx);
    for (int idx=sidx; idx<eidx; idx++){
      LinkedList *head = grid[idx];
      while (head!=NULL){
        int used = head->used;
        for (int n=0; n<used; n++){
          float *cur = &head->pt[n*4];
          // Compare point with points after it in current chunk
          for (int m=n+1; m<used; m++){
            if (dist2(cur, &head->pt[m * 4]) <= 0.0025) {
              npairs++;
            }
          }
          // Compare with following chunks in the same cell
          npairs += compare(cur, head->next);

          // Check neighbouring cells
          npairs += compare(cur, grid[idx + INDEX(1,  0,  0)]);
          npairs += compare(cur, grid[idx + INDEX(1,  1,  0)]);
          npairs += compare(cur, grid[idx + INDEX(1,  1,  1)]);
          npairs += compare(cur, grid[idx + INDEX(1,  1, -1)]);
          npairs += compare(cur, grid[idx + INDEX(1, -1,  0)]);
          npairs += compare(cur, grid[idx + INDEX(1, -1,  1)]);
          npairs += compare(cur, grid[idx + INDEX(1, -1, -1)]);
          npairs += compare(cur, grid[idx + INDEX(1,  0, -1)]);
          npairs += compare(cur, grid[idx + INDEX(1,  0,  1)]);
          npairs += compare(cur, grid[idx + INDEX(0,  1,  0)]);
          npairs += compare(cur, grid[idx + INDEX(0,  1,  1)]);
          npairs += compare(cur, grid[idx + INDEX(0,  1, -1)]);
          npairs += compare(cur, grid[idx + INDEX(0,  0,  1)]);
        }
        head = head->next;
      }
    }
    if (eidx >= end_idx){
      break;
    }
  }
  cstate.npairs += npairs;
  cstate.n_running--;
  return NULL;
}

long count(LinkedList **grid){
  const int nthreads = cstate.nthreads;
  cstate.threads = malloc(nthreads*sizeof(pthread_t));
  cstate.grid = grid;
  // Create threads
  for (int i=0; i<nthreads; i++){
    if (0 != pthread_create(&cstate.threads[i], NULL, &counter_thread, NULL)){
      perror("pthread_create");
      exit(-1);
    }
  }
  join_threads(nthreads, cstate.threads);
  free(cstate.threads);
  return cstate.npairs;
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
  printf("%ld\n", npairs);
  STOP_TIMER("count");

  START_TIMER();
  close(fd);
  munmap(file, size);
  free(grid);
  STOP_TIMER("cleanup");
  return 0;
}
