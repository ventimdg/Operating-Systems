/*
 * mm_alloc.c
 */

#include "mm_alloc.h"

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

struct metadata* head = NULL;

void* mm_malloc(size_t size) {
  //TODO: Implement malloc

  if (size == 0) {
    return NULL;
  }

  if (head ==  NULL){

    void* heap_ptr = sbrk(size + sizeof(struct metadata));

    if (heap_ptr == NULL || heap_ptr == (void*) -1) {
      return NULL;
    }
    
    head = (struct metadata*) heap_ptr;
    head->free = false;
    head->size = size;
    head->prev = NULL;
    head->next = NULL;
    head->data = heap_ptr + sizeof(struct metadata);
    memset(head->data, 0, size);
    return head->data;
  } else {
    struct metadata* last;
    for (struct metadata* cur = head; cur != NULL; cur = cur->next){
      if (cur->next == NULL){
        last = cur;
      }
      if (cur->free && cur->size >= size) {
        size_t gap =  cur->size - size;
        if (gap > sizeof(struct metadata)){

          cur->size = size;
          cur->free = false;
          memset(cur->data, 0, size);


          struct metadata* new = (struct metadata*) (cur->data +  size);
          new->free = true;
          new->size = gap - sizeof(struct metadata);
          new->data = (void*) ((char*) new + sizeof(struct metadata));
          memset(new->data, 0, new->size);

          new->next = cur->next;
          new->prev = cur;
          cur->next = new;

          if (new->next != NULL) {
            new->next->prev = new;
          }

        } else {
          cur->free = false;
          memset(cur->data, 0, cur->size);

        }
        return cur->data;
      }
    }
    void* new = sbrk(size + sizeof(struct metadata));
    if (new == NULL || new == (void*) -1) {
      return NULL;
    }
    struct metadata* new_metadata = (struct metadata*) new;
    new_metadata->free = false;
    new_metadata->size = size;
    new_metadata->next = NULL;
    new_metadata->prev = last;
    new_metadata->data = new + sizeof(struct metadata);
    memset(new_metadata->data, 0, size);
    last->next =  new_metadata;
    return new_metadata->data;

  }
}

void* mm_realloc(void* ptr, size_t size) {
  //TODO: Implement realloc
  if (ptr == NULL && size == 0){
    return NULL;
  }

  if (ptr == NULL){
    return mm_malloc(size);
  }

  if (size == 0){
    mm_free(ptr);
    return NULL;
  }

  void* new = mm_malloc(size);

  if (new == NULL) {
    return NULL;
  }

  size_t prev_size;
  struct metadata* prev_data;

  for (struct metadata* cur = head; cur != NULL; cur = cur->next){
    if (cur->data == ptr){
      prev_size = cur->size;
      prev_data = cur;
      break;
    }
  }

  for (struct metadata* cur = head; cur != NULL; cur = cur->next){
    if (cur->data == new){
      if (prev_size < size){
        memcpy(cur->data, prev_data->data, prev_size);
      } else {
        memcpy(cur->data, prev_data->data, size);
      }
      mm_free(prev_data->data);
      return cur->data;
    }
  }

  return NULL;
}

void mm_free(void* ptr) {
  //TODO: Implement free
  if (ptr == NULL || head == NULL){
    return;
  }

  for (struct metadata* cur = head; cur != NULL; cur = cur->next){
    if (cur->data == ptr){
      cur->free = true;
      if (cur->next != NULL && cur->next->free){
        cur->size += cur->next->size + sizeof(struct metadata);
        if (cur->next->next != NULL){
          cur->next->next->prev = cur;
          cur->next = cur->next->next;
        } else {
          cur->next = NULL;
        }
      }
      if (cur->prev != NULL && cur->prev->free){
        cur->prev->next =  cur->next;
        if (cur->next != NULL){
          cur->next->prev = cur->prev;
        }
        cur->prev->size += cur->size + sizeof(struct metadata);
      }
      return;
    }
  }
}
