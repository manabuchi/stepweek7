//
// >>>> malloc challenge! <<<<
//
// Your task is to improve utilization and speed of the following malloc
// implementation.
// Initial implementation is the same as the one implemented in simple_malloc.c.
// For the detailed explanation, please refer to simple_malloc.c.

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//
// Interfaces to get memory pages from OS
//

void *mmap_from_system(size_t size);
void munmap_to_system(void *ptr, size_t size);

//
// Struct definitions
//

typedef struct my_metadata_t {
  size_t size;
  struct my_metadata_t *next;
  struct my_metadata_t *prev; // 双方向の連結リストのためにprevを追加
} my_metadata_t;

typedef struct my_heap_t {
  my_metadata_t *free_head;
  my_metadata_t dummy;
} my_heap_t;

//
// Static variables (DO NOT ADD ANOTHER STATIC VARIABLES!)
//
my_heap_t my_heap;

//
// Helper functions (feel free to add/remove/edit!)
//

void my_add_to_free_list(my_metadata_t *metadata) {
  // metadataのnextを現在のfree_head（ダミーノードの次）に設定
  metadata->next = my_heap.free_head->next;
  // metadataのprevをダミーノードに設定
  metadata->prev = my_heap.free_head;

  if (my_heap.free_head->next != NULL) {
    my_heap.free_head->next->prev = metadata;
  }
  my_heap.free_head->next = metadata;
}

// TO SUPPORT DOUBLY LINKED LIST!
void my_remove_from_free_list(my_metadata_t *metadata) {
  // metadataがprevポインタを持っているので、引数prevは不要になった. 
  if (metadata->prev) {
    metadata->prev->next = metadata->next;
  }
  if (metadata->next) {
    metadata->next->prev = metadata->prev;
  }
  metadata->next = NULL;
  metadata->prev = NULL;
}

//
// Interfaces of malloc (DO NOT RENAME FOLLOWING FUNCTIONS!)
//

// This is called at the beginning of each challenge.
void my_initialize() {
  my_heap.free_head = &my_heap.dummy;
  my_heap.dummy.size = 0;
  my_heap.dummy.next = NULL;
  my_heap.dummy.prev = NULL;
}

// my_malloc() is called every time an object is allocated.
// |size| is guaranteed to be a multiple of 8 bytes and meets 8 <= |size| <=
// 4000. You are not allowed to use any library functions other than
// mmap_from_system() / munmap_to_system().
void *my_malloc(size_t size) {
  my_metadata_t *current = my_heap.free_head->next;
  my_metadata_t *best_fit_metadata = NULL;
  
  size_t min_diff = SIZE_MAX; //最小の差を追跡する

  //Best fit フリーリスト全体を追跡して最適なフィットを見つける
  while (current) {
    if (current->size >= size) { //現在のベストフィットよりも小さい差(より良い解)が見つかった
      size_t diff = current->size - size;
      if (diff < min_diff) {
        min_diff = diff;
        best_fit_metadata = current;
        if (diff == 0) { // ピッタリ賞が見つかった！これ以上良いフィットはないのでループを抜ける。challengeによってはこれで高速化されるかも？
          break;
        }
      }
      
    }
    current = current->next;
  }
  
  if (!best_fit_metadata) {
    // There was no free slot available (or no suitable one). 
    // We need to request a new memory region from the system by calling mmap_from_system().
    //
    //     | metadata | free slot |
    //     ^
    //     metadata
    //     <---------------------->
    //            buffer_size
    size_t buffer_size = 4096;
    // メタデータと確保されるべきメモリを考慮して、最低限のサイズを確保
    if (buffer_size < size + sizeof(my_metadata_t)) {
      buffer_size = size + sizeof(my_metadata_t);
    }
    // アラインメントのために、4096の倍数に切り上げる
    buffer_size = (buffer_size + 4095) & ~4095;

    my_metadata_t *metadata = (my_metadata_t *)mmap_from_system(buffer_size);
    metadata->size = buffer_size - sizeof(my_metadata_t);
    metadata->next = NULL;
    metadata->prev = NULL; // Initialize prev for new mmap'd block

    // Add the memory region to the free list.
    my_add_to_free_list(metadata);
    // Now, try my_malloc() again. This should succeed.
    return my_malloc(size);
  }

  // |ptr| is the beginning of the allocated object.
  //
  // ... | metadata | object | ...
  //     ^          ^
  //     metadata   ptr
  void *ptr = best_fit_metadata + 1;
  size_t remaining_size = best_fit_metadata->size - size;

  // Remove the free slot from the free list.
  my_remove_from_free_list(best_fit_metadata);

  if (remaining_size > sizeof(my_metadata_t)) {
    // Shrink the metadata for the allocated object
    // to separate the rest of the region corresponding to remaining_size.
    // If the remaining_size is not large enough to make a new metadata,
    // this code path will not be taken and the region will be managed
    // as a part of the allocated object.
    best_fit_metadata->size = size;
    // Create a new metadata for the remaining free slot.
    //
    // ... | metadata | object | metadata | free slot | ...
    //     ^          ^        ^
    //     metadata   ptr      new_metadata
    //                 <------><---------------------->
    //                   size       remaining size
    my_metadata_t *new_metadata = (my_metadata_t *)((char *)ptr + size);
    new_metadata->size = remaining_size - sizeof(my_metadata_t);
    new_metadata->next = NULL;
    new_metadata->prev = NULL; // Initialize prev for new_metadata
    // Add the remaining free slot to the free list.
    my_add_to_free_list(new_metadata);
  }
  return ptr;
}

// This is called every time an object is freed.  You are not allowed to
// use any library functions other than mmap_from_system / munmap_to_system.
void my_free(void *ptr) {
  // Look up the metadata. The metadata is placed just prior to the object.
  //
  // ... | metadata | object | ...
  //     ^          ^
  //     metadata   ptr
  my_metadata_t *metadata = (my_metadata_t *)ptr - 1;

  //まずは右隣りのアドレスをmergeできるか確認
  // next_block_candidate_addr、解放されたブロックのすぐ後ろのアドレスを定義
  my_metadata_t *next_block_candidate_addr = (my_metadata_t *)((char *)metadata + sizeof(my_metadata_t) + metadata->size);
  bool merge_with_next = false;
  my_metadata_t *next_free_block = NULL;

  // フリーリストを走査して、next_block_candidate_addr がフリーリストに存在するかチェック
  my_metadata_t *temp_current = my_heap.free_head->next; //ダミーノードの次から開始
  while(temp_current) {
    if (temp_current == next_block_candidate_addr) {
      merge_with_next = true;
      next_free_block = temp_current;
      break;
    }
    temp_current = temp_current->next;
  }

  // 次に左隣の(1個前)のアドレスをmergeできるか確認
  bool merge_with_prev = false;
  my_metadata_t *prev_free_block = NULL;

  temp_current = my_heap.free_head->next;
  while(temp_current) {
    // temp_currentの次がmetadataの先頭と一致するかを確認
    if (((char *)temp_current + sizeof(my_metadata_t) + temp_current->size) == (char *)metadata) {
      merge_with_prev = true;
      prev_free_block = temp_current;
      break;
    }
    temp_current = temp_current->next;
  }

  if (merge_with_next && merge_with_prev) {
    my_remove_from_free_list(prev_free_block);
    my_remove_from_free_list(next_free_block);

    prev_free_block->size += sizeof(my_metadata_t) + metadata->size + sizeof(my_metadata_t) + next_free_block->size;
    my_add_to_free_list(prev_free_block);
  } else if (merge_with_next) {
    my_remove_from_free_list(next_free_block); // next_free_blockをリストから削除
    metadata->size += sizeof(my_metadata_t) + next_free_block->size;
    my_add_to_free_list(metadata);
  } else if (merge_with_prev) {
    my_remove_from_free_list(prev_free_block); // prev_free_blockをリストから削除
    prev_free_block->size += sizeof(my_metadata_t) + metadata->size;
    my_add_to_free_list(prev_free_block);
  } else {
    my_add_to_free_list(metadata);
  }
}

// This is called at the end of each challenge.
void my_finalize() {
  // Nothing is here for now.
  // feel free to add something if you want!
}

void test() {
  // Implement here!
  assert(1 == 1); /* 1 is 1. That's always true! (You can remove this.) */
}