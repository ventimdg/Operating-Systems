#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include <stdint.h>
#include <list.h>

// At most 8MB can be allocated to the stack
// These defines will be used in Project 2: Multithreading
#define MAX_STACK_PAGES (1 << 11)
#define MAX_THREADS 127

/* PIDs and TIDs are the same type. PID should be
   the TID of the main thread of the process */
typedef tid_t pid_t;

/* Thread functions (Project 2: Multithreading) */
typedef void (*pthread_fun)(void*);
typedef void (*stub_fun)(pthread_fun, void*);

//typedef metadata_list_t to a list which acts as a Pintos list for the children's shared data
typedef struct list metadata_list_t;

//creating the file descriptor table struct
typedef struct file_descriptor {
  int fd;                 //file descriptor
  const char* file_name;  //name of the file
  struct file* open_file; //open file*
  struct list_elem elem;  //pintos list elem
} file_descriptor_t;

//making a new defintion for list
typedef struct list file_descriptor_list_t;

/* The process control block for a given process. Since
   there can be multiple threads per process, we need a separate
   PCB from the TCB. All TCBs in a process will have a pointer
   to the PCB, and the PCB will have a pointer to the main thread
   of the process, which is `special`. */
struct process {
  /* Owned by process.c. */
  uint32_t* pagedir;          /* Page directory. */
  char process_name[16];      /* Name of the main thread */
  struct thread* main_thread; /* Pointer to main thread */

  /*Adding a struct list child to act as a Pintos list 
  for all the shared datas of the children in its PCB*/
  metadata_list_t* child;

  //file descriptor tablestruct
  file_descriptor_list_t* file_descriptor_table;

  //File descriptor counter
  int fd_next;

  //Global lock for filesystem syscalls
  struct lock* filesys_lock;

  //Pointer to the executable so we can close it in process exit
  struct file* executable;
};

void userprog_init(void);

pid_t process_execute(const char* file_name);
int process_wait(pid_t);
void process_exit(int exit_code);
void process_activate(void);

bool is_main_thread(struct thread*, struct process*);
pid_t get_pid(struct process*);

tid_t pthread_execute(stub_fun, pthread_fun, void*);
tid_t pthread_join(tid_t);
void pthread_exit(void);
void pthread_exit_main(void);

/* Initialize a fd table list. */
void init_fdtable(file_descriptor_list_t* fdlist);

/* add a fd in a fd list. */
file_descriptor_t* add_fd(file_descriptor_list_t* fdlist, int fd, struct file* curfile,
                          const char* name);

/* Find a fd in a fd list. */
file_descriptor_t* find_fd(file_descriptor_list_t* fdlist, int fd);

//Find the file name in the fd table
file_descriptor_t* find_filename(file_descriptor_list_t* fdlist, const char* name);

//Close syscall
int process_file_close(int args);

#endif /* userprog/process.h */
