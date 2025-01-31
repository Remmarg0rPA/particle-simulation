#ifndef __HASHMAP_H
#define __HASHMAP_H
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "util.h"

#define CHUNK_SIZE 8
typedef struct ChunkList {
  float pt[4*CHUNK_SIZE];
  struct ChunkList *next;
  int used;
} ChunkList;

// Entries in the hash table
typedef struct hm_elem {
  struct hm_elem *next;
  uint64_t key;
  struct ChunkList *vals;
} hm_elem;

typedef struct Hashmap {
  size_t capacity;
  atomic_size_t used;
  hm_elem **elems;
} Hashmap;

void init_hashmap(struct Hashmap *hm, size_t capacity);
struct ChunkList *find_key(struct Hashmap *hm, uint64_t key);
void insert(struct Hashmap *hm, uint64_t key, float val[4]);
void flatten(struct Hashmap *hm, uint64_t **keyarr, size_t *size);

#endif //__HASHMAP_H

#ifdef HASHMAP_IMPLEMENTATION

INLINE uint64_t hash(struct Hashmap *hm, uint64_t key){
  uint64_t mod = hm->capacity;
  return (key ^ (key>>16)) % mod;
}

void init_hashmap(struct Hashmap *hm, size_t capacity){
  hm->capacity = capacity;
  hm->used = 0;
  hm->elems = (hm_elem **)calloc(capacity, sizeof(hm_elem *));
}

struct ChunkList *find_key(struct Hashmap *hm, uint64_t key){
  uint64_t idx = hash(hm, key);
  hm_elem *orig_head = hm->elems[idx];
  if (orig_head != NULL){
    hm_elem *head = orig_head;
    do {
      if (head->key == key){
        return head->vals;
      }
      head = head->next;
    } while (head != NULL);
  }
  return NULL;
}

/*
  Insert a new point into the list using an atomic exchange operation.
*/
void atomic_insert(struct hm_elem *node, float pt[4]){
  ChunkList *vs = node->vals;
  int used = __atomic_add_fetch(&vs->used, 1, __ATOMIC_RELAXED);
  // Thread filling last slot is responsible for creating a new chunk
  if (used == CHUNK_SIZE){
    ChunkList *chunk = (ChunkList *)calloc(1, sizeof(ChunkList));
    if (chunk == NULL) {
      perror("calloc");
      exit(-1);
    }
    // Move chunk into node->vals and and node->vals into chunk->next
    __atomic_exchange(&node->vals, &chunk, &chunk->next, __ATOMIC_RELAXED);
  } else if (used > CHUNK_SIZE){
    // Restore cell->used and loop back until a new chunk has been inserted
    vs->used = CHUNK_SIZE;
    atomic_insert(node, pt);
    return;
  }
  vs->pt[(used-1)*4 + 0] = pt[0];
  vs->pt[(used-1)*4 + 1] = pt[1];
  vs->pt[(used-1)*4 + 2] = pt[2];
  vs->pt[(used-1)*4 + 3] = pt[3];
}


void insert(struct Hashmap *hm, uint64_t key, float val[4]){
  uint64_t idx = hash(hm, key);
  hm_elem *orig_head = hm->elems[idx];
  // See if key already has an entry in the table
  if (orig_head != NULL){
    hm_elem *head = orig_head;
    do {
      if (head->key == key){
        atomic_insert(head, val);
        return;
      }
      head = head->next;
    } while (head != NULL);
  }
  // Create new entry for key
  hm_elem *entry = (hm_elem *)malloc(sizeof(hm_elem));
  entry->vals = (ChunkList *)malloc(sizeof(ChunkList));
  entry->key = key;
  entry->vals->pt[0] = val[0];
  entry->vals->pt[1] = val[1];
  entry->vals->pt[2] = val[2];
  entry->vals->pt[3] = val[3];
  entry->vals->used = 1;
  entry->vals->next = NULL;
  hm_elem *expected;
  expected = hm->elems[idx];
  entry->next = expected;
  // Move entry into the table if the table has not changed since it was read.
  while (!__atomic_compare_exchange(&hm->elems[idx], &entry->next, &entry, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)){
    // Traverse new entries and check for if key was inserted
    hm_elem *tmp = entry->next;
    while (tmp != expected){
      if (tmp->key == key){
        atomic_insert(tmp, val);
        return;
      }
      tmp = tmp->next;
    }
    expected = entry->next;
  }
  hm->used++;
}

void flatten(struct Hashmap *hm, uint64_t **keyarr, size_t *size){
  if (*size < (hm->used * sizeof(uint64_t))) {
    *keyarr = (uint64_t *)realloc(*keyarr, hm->used * sizeof(uint64_t));
    *size = hm->used * sizeof(uint64_t);
  }
  size_t len = hm->capacity;
  size_t idx = 0;
  uint64_t *ka = *keyarr;
  for (size_t i=0; i<len; i++){
    hm_elem *head = hm->elems[i];
    while (head != NULL){
      ka[idx] = head->key;
      idx++;
      head = head->next;
    }
  }
}
#endif // HASHMAP_IMPLEMENTATION
