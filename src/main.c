#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

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

typedef struct LinkedList {
  float *pt;
  struct LinkedList *next;
} LinkedList;

typedef struct parser_args {
  char *file_start;
  char *file_end;
  volatile float **data_next_empty;
  float *data_end;
} parser_args;

typedef struct counter_args {
  LinkedList **grid;
  int start_idx;
  int end_idx;
  int stride;
  volatile long npairs;
} counter_args;

typedef struct Config {
  int n_parser_threads;
  int n_count_threads;
  char *file;
} Config;

typedef struct parser_state {
  int nthreads;
  pthread_t *threads;
  parser_args *threads_args;
  volatile float *data_next_empty;
  volatile int n_running;
} parser_state;


Config config = {
  .n_parser_threads = 4,
  .n_count_threads = 4,
  .file = NULL
};

parser_state pstate = {
  .nthreads = 4,
  .threads = NULL,
  .threads_args = NULL,
  .data_next_empty = NULL,
  .n_running = 0,
};

void usage(char **argv){
  printf("Usage: %s -f <data file> [-p NUM] [-c NUM]\n", argv[0]);
  puts("\t-c Num\tNumber of threads to use while counting pairs. Default is 4.");
  puts("\t-p Num\tNumber of threads to use while parsing. Default is 4.");
  puts("\t-h\tPrint this message and exit.");
}

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

/*
  Parses 3 floats.
  NOTE: Assumes that the first char is the start of the float
  and that they are separated by only 1 whitespace.
 */
static inline char *parse_line(char *str, float *data) {
  char *end = NULL;
  #pragma GCC unroll 3
  for (int i=0; i<3; i++){
    *data = strtof(str, &end);
    str = end+1;
    data++;
  }
  return str;
}

void *parser_thread(void *__pargs){
  pstate.n_running += 1;
  parser_args *pargs = (parser_args *)__pargs;
  char *str = pargs->file_start;
  float *data_ptr = NULL;
  while (str < pargs->file_end){
    data_ptr = (float *)__atomic_fetch_add(pargs->data_next_empty, 3*sizeof(float), __ATOMIC_SEQ_CST);
    str = parse_line(str, data_ptr);
  }
  if (data_ptr >= pargs->data_end){
    pstate.n_running -= 1;
    perror("parser overflow");
    exit(-1);
  }
  pstate.n_running -= 1;
  return NULL;
}

void start_parser(char *file, float *data, size_t size){
  const int nthreads = pstate.nthreads;
  pstate.threads = malloc(nthreads*sizeof(pthread_t));
  pstate.threads_args = malloc(nthreads*sizeof(parser_args));
  if (pstate.threads == NULL || pstate.threads_args == NULL){
    perror("malloc");
    exit(-1);
  }
  pstate.data_next_empty = data;

  // Init args to threads
  pstate.threads_args[0].file_start = file;
  pstate.threads_args[0].data_next_empty = &pstate.data_next_empty;
  pstate.threads_args[0].data_end = data+size;
  for (int i=1; i<nthreads; i++){
    pstate.threads_args[i].file_start = file + (size/nthreads)*i;
    pstate.threads_args[i].data_next_empty = &pstate.data_next_empty;
    pstate.threads_args[i].data_end = data+size;
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

void join_parser(void){
  int nthreads = config.n_parser_threads;
  for (int i=0; i<nthreads; i++){
    if (0 != pthread_join(pstate.threads[i], NULL)){
      perror("pthread_join");
      exit(-1);
    }
  }
}

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

long count(LinkedList **grid){
  const int nthreads = config.n_count_threads;
  pthread_t *threads = malloc(nthreads*sizeof(pthread_t));
  counter_args *cargs = malloc(nthreads*sizeof(counter_args));
  for (int i=0; i<nthreads; i++){
    cargs[i].grid = grid;
    cargs[i].start_idx = i;
    cargs[i].end_idx = NBB;
    cargs[i].stride = nthreads;
  }
  // Create threads
  for (int i=0; i<nthreads; i++){
    if (0 != pthread_create(&threads[i], NULL, &counter_thread, &cargs[i])){
      perror("pthread_create");
      exit(-1);
    }
  }
  long npairs = 0;
  // Join threads
  for (int i=0; i<nthreads; i++){
    if (0 != pthread_join(threads[i], NULL)){
      perror("pthread_join");
      exit(-1);
    }
    npairs += cargs[i].npairs;
  }
  free(threads);
  free(cargs);
  return npairs;
}

int main(int argc, char **argv){
  START_TIMER();
  int i = 1;
  while (i < argc) {
    switch (argv[i][0]){
    case '-':
      switch (argv[i][1]){
      case 'c':
        i++;
        if (0 >= (config.n_count_threads = atoi(argv[i]))){
          perror("atoi");
          return -1;
        }
        break;
      case 'f':
        i++;
        config.file = argv[i];
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

  int fd = open(config.file, O_RDONLY);
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

  // This will be a small overestimation of needed space for positions.xyz,
  // and a large for positions_large.xyz
  // NOTE: Using MAP_ANONYMOUS appears to be much slower than not using it here.
  float *data = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
  if (data == MAP_FAILED){
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
  start_parser(file, data, size);
  join_parser();
  STOP_TIMER("parsing");

  START_TIMER();
  long used = pstate.data_next_empty - data;
  insert_into_grid(grid, data, used);
  STOP_TIMER("insertion");

  START_TIMER();
  long npairs = count(grid);
  STOP_TIMER("count");

  printf("%ld\n", npairs);

  START_TIMER();
  close(fd);
  munmap(file, size);
  munmap(data, size);
  free(grid);
  STOP_TIMER("cleanup");
  return 0;
}
