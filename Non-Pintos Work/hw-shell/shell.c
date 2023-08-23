#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(struct tokens* tokens);
int cmd_help(struct tokens* tokens);
int cmd_pwd(struct tokens* tokens);
int cmd_cd(struct tokens* tokens);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens* tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t* fun;
  char* cmd;
  char* doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
    {cmd_help, "?", "show this help menu"},
    {cmd_exit, "exit", "exit the command shell"},
    {cmd_pwd, "pwd", "print current working directory"},
    {cmd_cd, "cd", "change current working directory"},

};

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens* tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens* tokens) { exit(0); }

/*print current working directory*/
int cmd_pwd(unused struct tokens* tokens) {
  char* dir = getcwd(NULL, 0);
  printf("%s", dir);
  printf("\n");
  free(dir);
  return 1;
}

int cmd_cd(struct tokens* tokens) {
  char* dir = tokens_get_token(tokens, 1);
  chdir(dir);
  return 1;
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

int main(unused int argc, unused char* argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);


  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens* tokens = tokenize(line);


    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {
      /* REPLACE this to run commands as programs. */
      char* path = tokens_get_token(tokens, 0);
      int size = tokens_get_length(tokens);
      int stop = -1;
      int numpipes = 0;

      for (int i = 0; i < size; i++){
        char* curr = tokens_get_token(tokens, i);
        if (strcmp(curr, "|") == 0 ){
          numpipes++;
        }
      }
      
      if (numpipes > 0) {
        int start[numpipes];
        int pos = 0;

        for (int i = 0; i < size; i++){
          char* curr = tokens_get_token(tokens, i);
          if (strcmp(curr, "|") == 0 ){
            start[pos] = i;
            pos++;
          }
        }

        pos = 0;
        int pipes[numpipes][2];


        for(int i = 0; i <= numpipes; i++){
          if (i == 0){
            for (int i = 0; i < start[pos]; i++){
              char* curr = tokens_get_token(tokens, i);
              if (strcmp(curr, "<") == 0 || strcmp(curr, ">") == 0){
                stop = i; 
                break;
              }
            }
            if (stop == -1){
              stop = start[pos];
            }
            char* args[stop + 1];
            args[stop] = NULL;
            for (int i = 0; i < stop; i++){
              char* curr = tokens_get_token(tokens, i);
              args[i] = (char *) malloc((strlen(curr) + 1) * sizeof(char));
              strcpy(args[i], curr);
            }
        
            pipe(pipes[pos]);
            pid_t pid = fork();
      
            if (pid == 0) { 

              signal(SIGINT, SIG_DFL);
              signal(SIGTSTP, SIG_DFL);
              signal(SIGQUIT, SIG_DFL);

              close(pipes[pos][0]);
             
              bool left_arr = false;
              bool right_arr = false; 
              char* input_file = NULL;
              char* output_file = NULL;

              for (int i = stop; i < size; i++){
                char* curr = tokens_get_token(tokens, i);
                if (strcmp(curr, "<") == 0) {
                  left_arr = true;
                  i++;
                  input_file = tokens_get_token(tokens, i);
                } 
                else if (strcmp(curr, ">") == 0) {
                  right_arr = true;
                  i++;
                  output_file = tokens_get_token(tokens, i);
                }
              }

              if (left_arr){
                int input_fd = open(input_file, O_RDWR | O_CREAT, S_IRWXU);
                dup2(input_fd, 0);
              }
              if (right_arr){
                int output_fd = open(output_file, O_RDWR | O_CREAT, S_IRWXU);
                dup2(output_fd, 1);
              }
              dup2(pipes[pos][1], 1);
              close(pipes[pos][1]);

              execv(path, args);
              char* allpaths =  (char*) malloc((strlen(getenv("PATH")) + 1) * sizeof(char)); 
              strcpy(allpaths, getenv("PATH"));
              char* saveptr =  (char*) malloc((strlen(getenv("PATH")) + 1) * sizeof(char)); 
              for (char* token = strtok_r(allpaths, ":", &saveptr); token != NULL; token = strtok_r(NULL, ":", &saveptr)){
                size_t len = strlen(token) + strlen(args[0]);
                char *newpath = (char*) malloc(len * sizeof(char) + 2);
                *newpath = '\0';
                strcat(newpath, token);
                strcat(newpath, "/");
                strcat(newpath,args[0]);
                int success = execv(newpath, args);
                if (success >= 0){
                  exit(0);
                }
              }
              exit(-1);
            } else {
              close(pipes[pos][1]);
              wait(&pid);
              for (int j = 0;  j < stop; j++) {
                free(args[j]);
              }
            }
          } else if (i == numpipes){
            pid_t pid = fork();
          
            if (pid == 0) {

              signal(SIGINT, SIG_DFL);
              signal(SIGTSTP, SIG_DFL);
              signal(SIGQUIT, SIG_DFL);

              bool left_arr = false;
              bool right_arr = false; 
              char* input_file = NULL;
              char* output_file = NULL;

              for (int i = start[pos]; i < size; i++){
                char* curr = tokens_get_token(tokens, i);
                if (strcmp(curr, "<") == 0) {
                  left_arr = true;
                  i++;
                  input_file = tokens_get_token(tokens, i);
                } 
                else if (strcmp(curr, ">") == 0) {
                  right_arr = true;
                  i++;
                  output_file = tokens_get_token(tokens, i);
                }
              }

              if (left_arr){
                int input_fd = open(input_file, O_RDWR | O_CREAT, S_IRWXU);
                dup2(input_fd, 0);
              }
              if (right_arr){
                int output_fd = open(output_file, O_RDWR | O_CREAT, S_IRWXU);
                dup2(output_fd, 1);
              }


              dup2(pipes[pos][0], 0);
              close(pipes[pos][0]);

              char* path = tokens_get_token(tokens, start[pos] + 1);
              stop = -1;

              for (int i = start[pos] + 1; i < size ; i++){
                char* curr = tokens_get_token(tokens, i);
                if (strcmp(curr, "<") == 0 || strcmp(curr, ">") == 0){
                  stop = i; 
                  break;
                }
              }
              if (stop == -1){
                stop = size;
              }
              char* args[stop - start[pos]];
              args[stop - start[pos] - 1] = NULL;
              for (int i = start[pos] + 1; i < stop ; i++){
                char* curr = tokens_get_token(tokens, i);
                args[i - (start[pos] + 1)] = (char *) malloc((strlen(curr) + 1) * sizeof(char));
                strcpy(args[i - (start[pos] + 1)], curr);
              }

              // for (int i = 0; i < stop - start[pos]; i++)
              //   printf("%s", args[i]);
              
              execv(path, args);
              char* allpaths =  (char*) malloc((strlen(getenv("PATH")) + 1) * sizeof(char)); 
              strcpy(allpaths, getenv("PATH"));
              char* saveptr =  (char*) malloc((strlen(getenv("PATH")) + 1) * sizeof(char)); 
              for (char* token = strtok_r(allpaths, ":", &saveptr); token != NULL; token = strtok_r(NULL, ":", &saveptr)){
                size_t len = strlen(token) + strlen(path);
                char *newpath = (char*) malloc(len * sizeof(char) + 2);
                *newpath = '\0';
                strcat(newpath, token);
                strcat(newpath, "/");
                strcat(newpath, path);
                int success = execv(newpath, args);
                if (success >= 0){
                  exit(0);
                }
              }
              exit(-1);
            } else {
              wait(&pid);
              close(pipes[pos][0]);
            }
          }else{
            pos++;
            pipe(pipes[pos]);
            pid_t pid = fork();
            
            if (pid == 0) {  

              signal(SIGINT, SIG_DFL);
              signal(SIGTSTP, SIG_DFL); 
              signal(SIGQUIT, SIG_DFL); 

              close(pipes[pos][0]);
              dup2(pipes[pos-1][0], 0);
              close(pipes[pos-1][0]);
              dup2(pipes[pos][1], 1);
              close(pipes[pos][1]);

              char* path = tokens_get_token(tokens, start[pos - 1] + 1);
  
              stop = -1;

              for (int i = start[pos-1] + 1; i <= start[pos] ; i++){
                char* curr = tokens_get_token(tokens, i);
                if (strcmp(curr, "|") == 0){
                  stop = i; 
                  break;
                }
              }
              if (stop == -1){
                stop = size;
              }
              char* args[stop - start[pos - 1]];
              args[stop - start[pos - 1] - 1] = NULL;
              for (int i = start[pos - 1] + 1; i < stop ; i++){
                char* curr = tokens_get_token(tokens, i);
                args[i - (start[pos - 1] + 1)] = (char *) malloc((strlen(curr) + 1) * sizeof(char));
                strcpy(args[i - (start[pos - 1] + 1)], curr);
              }

              
              execv(path, args);

              char* allpaths =  (char*) malloc((strlen(getenv("PATH")) + 1) * sizeof(char)); 
              strcpy(allpaths, getenv("PATH"));
              char* saveptr =  (char*) malloc((strlen(getenv("PATH")) + 1) * sizeof(char)); 
              for (char* token = strtok_r(allpaths, ":", &saveptr); token != NULL; token = strtok_r(NULL, ":", &saveptr)){
                size_t len = strlen(token) + strlen(path);
                char *newpath = (char*) malloc(len * sizeof(char) + 2);
                *newpath = '\0';
                strcat(newpath, token);
                strcat(newpath, "/");
                strcat(newpath, path);
                int success = execv(newpath, args);
                if (success >= 0){
                  exit(0);
                }
              }
              exit(-1);
            } else {
              close(pipes[pos][1]);
              wait(&pid);
              close(pipes[pos-1][0]);
            }
          }
        } 
      } else {
        for (int i = 0; i < size; i++){
          char* curr = tokens_get_token(tokens, i);
          if (strcmp(curr, "<") == 0 || strcmp(curr, ">") == 0){
            stop = i; 
            break;
          }
        }
        if (stop == -1){
          stop = size;
        }
        char* args[stop + 1];
        args[stop] = NULL;
        for (int i = 0; i < stop; i++){
          char* curr = tokens_get_token(tokens, i);
          args[i] = (char *) malloc((strlen(curr) + 1) * sizeof(char));
          strcpy(args[i], curr);
        }

        pid_t pid = fork();

        if (pid == 0){

          signal(SIGINT, SIG_DFL);
          signal(SIGTSTP, SIG_DFL);
          signal(SIGQUIT, SIG_DFL);

          bool left_arr = false;
          bool right_arr = false; 
          char* input_file = NULL;
          char* output_file = NULL;

          for (int i = stop; i < size; i++){
            char* curr = tokens_get_token(tokens, i);
            if (strcmp(curr, "<") == 0) {
              left_arr = true;
              i++;
              input_file = tokens_get_token(tokens, i);
            } 
            else if (strcmp(curr, ">") == 0) {
              right_arr = true;
              i++;
              output_file = tokens_get_token(tokens, i);
            }
          }

          if (left_arr){
            int input_fd = open(input_file, O_RDWR | O_CREAT, S_IRWXU);
            dup2(input_fd, 0);
          }
          if (right_arr){
            int output_fd = open(output_file, O_RDWR | O_CREAT, S_IRWXU);
            dup2(output_fd, 1);
          }

          execv(path, args);
          char* allpaths =  (char*) malloc((strlen(getenv("PATH")) + 1) * sizeof(char)); 
          strcpy(allpaths, getenv("PATH"));
          char* saveptr =  (char*) malloc((strlen(getenv("PATH")) + 1) * sizeof(char)); 
          for (char* token = strtok_r(allpaths, ":", &saveptr); token != NULL; token = strtok_r(NULL, ":", &saveptr)){
            size_t len = strlen(token) + strlen(args[0]);
            char *newpath = (char*) malloc(len * sizeof(char) + 2);
            *newpath = '\0';
            strcat(newpath, token);
            strcat(newpath, "/");
            strcat(newpath,args[0]);
            int success = execv(newpath, args);
            if (success >= 0){
              exit(0);
            }
          }
          exit(-1);
        }else if (pid == -1){
          exit(-1);
        } else {
          wait(&pid);
          for (int j = 0;  j < stop; j++) {
            free(args[j]);
          }
        }
      }
    }
    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }
  return 0;
}
