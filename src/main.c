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
#include "util.h"

#define HASHMAP_IMPLEMENTATION
#include "hashmap.h"

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

#define BBSIZE 0.05
#define NBB (int)((10-(-10))/BBSIZE+1 + 2)
#define INDEX(i,j,k) ((i)*NBB*NBB + (j)*NBB + (k))

typedef struct parser_state {
  int nthreads;
  pthread_t *threads;
  volatile Hashmap *hmap;
  char *file;
  size_t file_size;
  volatile atomic_int n_running;
  volatile int has_started;
} parser_state;

typedef struct counter_state {
  int nthreads;
  pthread_t *threads;
  Hashmap *hmap;
  uint64_t *keys;
  volatile size_t keys_next_index;
  size_t keys_len;
  atomic_long npairs;
  volatile atomic_int n_running;
  volatile int has_started;
} counter_state;

parser_state pstate = {
  .nthreads = 4,
  .threads = NULL,
  .hmap = NULL,
  .file = NULL,
  .file_size = 0,
  .n_running = 0,
  .has_started = 0,
};

counter_state cstate = {
  .nthreads = 4,
  .threads = NULL,
  .hmap = NULL,
  .keys = NULL,
  .keys_next_index = 0,
  .keys_len = 0,
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

INLINE float dist2(float *pt1, float *pt2){
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
  volatile Hashmap *bt = pstate.hmap;
  while (str < file_end){
    str = parse_line(str, data_buf);
    int x = (int)((data_buf[0] - (-10.))/BBSIZE);
    int y = (int)((data_buf[1] - (-10.))/BBSIZE);
    int z = (int)((data_buf[2] - (-10.))/BBSIZE);
    insert((Hashmap *)bt, INDEX(x,y,z), data_buf);
  }
  pstate.n_running--;
  return NULL;
}

void start_parser(char *file, size_t size, Hashmap *hmap){
  const int nthreads = pstate.nthreads;
  pstate.threads = malloc(nthreads*sizeof(pthread_t));
  if (pstate.threads == NULL){
    perror("malloc");
    exit(-1);
  }
  pstate.hmap = hmap;
  pstate.file = file;
  pstate.file_size = size;
  for (int i=0; i<nthreads; i++){
    if (0 != pthread_create(&pstate.threads[i], NULL, &parser_thread, NULL)){
      perror("pthread_create");
      exit(-1);
    }
  }
}

INLINE int count_chunk(float pt[4], ChunkList *chunk){
  int count = 0;
  int used = chunk->used;
  for (int i=used-1; i>=0; i--){
    if (dist2(pt, &chunk->pt[i*4])<=0.0025){
      count++;
    }
  }
  return count;
}

INLINE int compare(float *pt, ChunkList *head){
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
  Hashmap *hmap = cstate.hmap;
  uint64_t *keys = cstate.keys;
  volatile size_t *next_key_index = &cstate.keys_next_index;
  size_t length = cstate.keys_len;
  size_t idx;
  long npairs = 0;

  // Fetch an interval of chunk_size and iterate through the grid entries
  while (length > (idx = __atomic_fetch_add(next_key_index, 1, __ATOMIC_RELAXED))){
    // keys[idx] should always exist
    ChunkList *head = find_key(hmap, keys[idx]);
    ChunkList *neighbours[13] = {
      find_key(hmap, keys[idx] + INDEX(1,  0,  0)),
      find_key(hmap, keys[idx] + INDEX(1,  1,  0)),
      find_key(hmap, keys[idx] + INDEX(1,  1,  1)),
      find_key(hmap, keys[idx] + INDEX(1,  1, -1)),
      find_key(hmap, keys[idx] + INDEX(1, -1,  0)),
      find_key(hmap, keys[idx] + INDEX(1, -1,  1)),
      find_key(hmap, keys[idx] + INDEX(1, -1, -1)),
      find_key(hmap, keys[idx] + INDEX(1,  0, -1)),
      find_key(hmap, keys[idx] + INDEX(1,  0,  1)),
      find_key(hmap, keys[idx] + INDEX(0,  1,  0)),
      find_key(hmap, keys[idx] + INDEX(0,  1,  1)),
      find_key(hmap, keys[idx] + INDEX(0,  1, -1)),
      find_key(hmap, keys[idx] + INDEX(0,  0,  1)),
    };
    while (head!=NULL){
      int used = head->used;
      for (int n=used-1; n>=0; n--){
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
        for (int i=12; i>=0; i--){
          npairs += compare(cur, neighbours[i]);
        }
      }
      head = head->next;
    }
  }
  cstate.npairs += npairs;
  cstate.n_running--;
  return NULL;
}

long count(Hashmap *hmap, uint64_t *keys, size_t keys_len){
  const int nthreads = cstate.nthreads;
  cstate.threads = malloc(nthreads*sizeof(pthread_t));
  cstate.hmap = hmap;
  cstate.keys = keys;
  cstate.keys_len = keys_len;
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
  long file_size = sb.st_size;

  char *file = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (file == MAP_FAILED){
    perror("mmap");
    return -1;
  }
  Hashmap *hmap = malloc(sizeof(Hashmap));
  init_hashmap(hmap, file_size/128);
  STOP_TIMER("init");

  START_TIMER();
  start_parser(file, file_size, hmap);
  join_threads(pstate.nthreads, pstate.threads);
  hmap = (Hashmap *)pstate.hmap;
  close(fd);
  munmap(file, file_size);
  STOP_TIMER("parsing");

  START_TIMER();
  uint64_t *keys = NULL;
  size_t size = 0;
  flatten((Hashmap *)pstate.hmap, &keys, &size);
  STOP_TIMER("flatten");

  START_TIMER();
  long npairs = count(hmap, keys, hmap->used);
  printf("%ld\n", npairs);
  STOP_TIMER("count");

  return 0;
}
