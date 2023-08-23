/*
 * Implementation of the word_count interface using Pintos lists and pthreads.
 *
 * You may modify this file, and are expected to modify it.
 */

/*
 * Copyright © 2021 University of California, Berkeley
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#define PINTOS_LIST
#define PTHREADS

#ifndef PINTOS_LIST
#error "PINTOS_LIST must be #define'd when compiling word_count_lp.c"
#endif

#ifndef PTHREADS
#error "PTHREADS must be #define'd when compiling word_count_lp.c"
#endif

#include "word_count.h"

void init_words(word_count_list_t* wclist) {
  /* TODO */
  if (wclist == NULL){
    perror("Given List is Null");
  }
  list_init(&(wclist->lst));
  pthread_mutex_init(&(wclist->lock), NULL);

}

size_t len_words(word_count_list_t* wclist) {
  /* TODO */
  if (wclist == NULL){
    perror("Given List is Null");
  }
  return list_size(&(wclist->lst));
}

word_count_t* find_word(word_count_list_t* wclist, char* word) {
  /* TODO */
  if (wclist == NULL){
    perror("Given List is NULL");
  }
  if (word == NULL){
    perror("Given word is NULL");
  }
  struct list_elem *e;
  for (e = list_begin(&(wclist->lst)); e != list_end(&(wclist->lst)); e = list_next(e)) {
    struct word_count *curr_word = list_entry(e, struct word_count, elem);
    //word_count_t *curr_word = list_entry(e, word_count_t, elem);
    if (strcmp(curr_word->word, word) == 0){
      return curr_word;
    }
  }
  return NULL;
}

word_count_t* add_word(word_count_list_t* wclist, char* word) {
  /* TODO */
  if (wclist == NULL){
    perror("Given List is NULL");
  }
  if (word == NULL){
    perror("Given word is NULL");
  }
  pthread_mutex_lock(&(wclist->lock));
  word_count_t* found = find_word(wclist, word);
  if (found != NULL){
    found->count++;
    pthread_mutex_unlock(&(wclist->lock));
    return found;
  } else {
    word_count_t* new_elem = (word_count_t*) malloc(sizeof(word_count_t));
    if (new_elem == NULL){
      perror("Failed to malloc new word_count_t struct");
    }
    new_elem->word = (char*) malloc(sizeof(char) * strlen(word) + 1);
    if (new_elem->word == NULL){
      free(new_elem);
      perror("Failed to malloc new word");
    } else {
      strcpy(new_elem->word, word);
    }
    new_elem->count = 1;
    list_insert(list_end(&(wclist->lst)), &(new_elem->elem));
    pthread_mutex_unlock(&(wclist->lock));
    return new_elem;
  }
}

void fprint_words(word_count_list_t* wclist, FILE* outfile) {
  /* TODO */
  if (wclist == NULL){
    perror("Given List is NULL");
  }
  if (wclist == NULL){
    perror("Given outfile is NULL");
  }
  struct list_elem *e;
  for (e = list_begin(&(wclist->lst)); e != list_end(&(wclist->lst)); e = list_next(e)) {
    struct word_count *curr_word = list_entry(e, struct word_count, elem);
    fprintf(outfile, "%i\t%s\n", curr_word->count, curr_word->word);
  }
}

static bool less_list(const struct list_elem* ewc1, const struct list_elem* ewc2, void* aux) {
  /* TODO */
  bool (*compare)(word_count_t*, word_count_t*) = aux;
  return compare(list_entry(ewc1, struct word_count, elem), list_entry(ewc2, struct word_count, elem));
}

void wordcount_sort(word_count_list_t* wclist,
        bool less(const word_count_t*, const word_count_t*)) {
  /* TODO */
  list_sort(&(wclist->lst), less_list, less);
}
