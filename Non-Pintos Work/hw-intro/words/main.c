/*

  Word Count using dedicated lists

*/

/*
Copyright © 2019 University of California, Berkeley

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include <assert.h>
#include <getopt.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>

#include "word_count.h"

/* Global data structure tracking the words encountered */
WordCount *word_counts = NULL;

/* The maximum length of each word in a file */
#define MAX_WORD_LEN 64

/*
 * 3.1.1 Total Word Count
 *
 * Returns the total amount of words found in infile.
 * Useful functions: fgetc(), isalpha().
 */
int num_words(FILE* infile) {
  int num_words = 0;
  bool incr = true;
  char c = fgetc(infile);
  int len = 0;

  while (c != EOF) {
    if (!isalpha(c) && !isdigit(c)){
      if(incr && len > 1 && len <= 64){
        num_words++;
      }
      incr = true;
      c = fgetc(infile);
      len = 0;
    } else if (!isalpha(c)){
      incr = false;
      if(len > 1 && len <= 64){
        num_words++;
      }
      len = 0;
      c = fgetc(infile);
    } else {
      c = fgetc(infile);
      incr = true;
      len++;
    }
  }
  if (incr && len > 1 && len <= 64){
    num_words++;
  }
  return num_words;
}

/*
 * 3.1.2 Word Frequency Count
 *
 * Given infile, extracts and adds each word in the FILE to `wclist`.
 * Useful functions: fgetc(), isalpha(), tolower(), add_word().
 * 
 * As mentioned in the spec, your code should not panic or
 * segfault on errors. Thus, this function should return
 * 1 in the event of any errors (e.g. wclist or infile is NULL)
 * and 0 otherwise.
 */
int count_words(WordCount **wclist, FILE *infile) {
  if (wclist == NULL || infile == NULL){
    return 1;
  }
  char words[MAX_WORD_LEN + 1];

  // char c;
  // int curchar = 0;
  // bool waiting = false;
  
  // while ((c = fgetc(infile)) != EOF) {
  //   if (c == ' ' || c == '\n'){
  //     if (!waiting){
  //       words[curchar] = '\0';
  //       char* src = words;
  //       char* dest = (char *) malloc(sizeof(char) * (strlen(src)+1));
  //       if (dest == NULL){
  //         return 1;
  //       }
  //       strcpy(dest, src);
  //       add_word(wclist, dest);
  //       curchar = 0;

  //     } else {
  //       waiting = false;
  //     }
  //   } else if (isalpha(c) && !waiting){
  //     c = tolower(c);
  //     words[curchar] = c;
  //     curchar++;
  //   } else {
  //     curchar = 0;
  //     waiting = true;
  //   }
  // }

  bool incr = false;
  char c = fgetc(infile);
  int len = 0;

  while (c != EOF) {

    if (!isalpha(c)){
      if(incr && len > 1 && len <= 64){
        words[len] = '\0';
        char* src = words;
        char* dest = (char *) malloc(sizeof(char) * (strlen(src)+1));
        if (dest == NULL){
          return 1;
        }
        strcpy(dest, src);
        add_word(wclist, dest);
      }
      incr = false;
      c = fgetc(infile);
      len = 0;
    } else {
      c = tolower(c);
      words[len] = c;
      len++;
      c = fgetc(infile);
      incr = true;
    }
  }
  if (incr && len > 1 && len <= 64){
    words[len] = '\0';
    char* src = words;
    char* dest = (char *) malloc(sizeof(char) * (strlen(src)+1));
    if (dest == NULL){
      return 1;
    }
    strcpy(dest, src);
    add_word(wclist, dest);
  }
  return 0;
}

/*
 * Comparator to sort list by frequency.
 * Useful function: strcmp().
 */
static bool wordcount_less(const WordCount *wc1, const WordCount *wc2) {
  if (wc1->count < wc2->count){
    return true;
  } else if (wc1->count > wc2->count) {
    return false;
  } else {
    if (wc1->word == NULL){
      return true;
    }
    if (wc2->word == NULL){
      return false;
    }
    int diff = strcmp(wc1->word, wc2->word);
    if (diff > 0){
      return false;
    } else {
      return true;
    }
  }
  return false;
}

// In trying times, displays a helpful message.
static int display_help(void) {
	printf("Flags:\n"
	    "--count (-c): Count the total amount of words in the file, or STDIN if a file is not specified. This is default behavior if no flag is specified.\n"
	    "--frequency (-f): Count the frequency of each word in the file, or STDIN if a file is not specified.\n"
	    "--help (-h): Displays this help message.\n");
	return 0;
}

/*
 * Handle command line flags and arguments.
 */
int main (int argc, char *argv[]) {

  // Count Mode (default): outputs the total amount of words counted
  bool count_mode = true;
  int total_words = 0;
  int err;

  // Freq Mode: outputs the frequency of each word
  bool freq_mode = false;

  FILE *infile = NULL;

  // Variables for command line argument parsing
  int i;
  static struct option long_options[] =
  {
      {"count", no_argument, 0, 'c'},
      {"frequency", no_argument, 0, 'f'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
  };

  // Sets flags
  while ((i = getopt_long(argc, argv, "cfh", long_options, NULL)) != -1) {
      switch (i) {
          case 'c':
              count_mode = true;
              freq_mode = false;
              break;
          case 'f':
              count_mode = false;
              freq_mode = true;
              break;
          case 'h':
              return display_help();
      }
  }

  if (!count_mode && !freq_mode) {
    printf("Please specify a mode.\n");
    return display_help();
  }

  /* Create the empty data structure */
  init_words(&word_counts);

  if ((argc - optind) < 1) {
    // No input file specified, instead, read from STDIN instead.
    infile = stdin;
    if (infile == NULL){
      return 1;
    }
    if(count_mode) {
      total_words += num_words(infile);
    } else {
      err = count_words(&word_counts, infile);
      if (err == 1){
        return 1;
      }
    }
  } else {
    // At least one file specified. Useful functions: fopen(), fclose().
    // The first file can be found at argv[optind]. The last file can be
    // found at argv[argc-1].
    for(int i = optind; i < argc; i++){
      infile = fopen(argv[i], "r");
      if (infile == NULL){
        fprintf(stderr, "File does not exist.\n");
        return 1;
      }
      if(count_mode) {
        total_words += num_words(infile);
      } else {
        err = count_words(&word_counts, infile);
        if (err == 1){
          return 1;
        }
      }
      fclose(infile);
    }
  }

  if (count_mode) {
    printf("The total number of words is: %i\n", total_words);
  } else {
    wordcount_sort(&word_counts, wordcount_less);
    word_counts = word_counts->next;
    if (word_counts->count != 0) {
      printf("The frequencies of each word are: \n");
      fprint_words(word_counts, stdout);
    }
}
  return 0;
}
