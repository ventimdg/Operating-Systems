#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static struct semaphore temporary;
static thread_func start_process NO_RETURN;
static thread_func start_pthread NO_RETURN;
static bool load(const char* file_name, void (**eip)(void), void** esp);
bool setup_thread(void (**eip)(void), void** esp);

//Creating the global file system lock for file syscalls
struct lock filesys_lock;

//initalize a new file descriptor table
void init_fdtable(file_descriptor_list_t* fdlist) {
  /* TODO */
  if (fdlist == NULL) {
    return;
  }
  list_init(fdlist);
}

//find a fd in the fd table
file_descriptor_t* find_fd(file_descriptor_list_t* fdlist, int fd) {
  /* TODO */
  if (fdlist == NULL) {
    return NULL;
  }
  struct list_elem* e;
  for (e = list_begin(fdlist); e != list_end(fdlist); e = list_next(e)) {
    struct file_descriptor* curr_fd = list_entry(e, struct file_descriptor, elem);
    if (curr_fd->fd == fd) {
      return curr_fd;
    }
  }
  return NULL;
}

//Find the file name in the fd table
file_descriptor_t* find_filename(file_descriptor_list_t* fdlist, const char* name) {
  /* TODO */
  if (fdlist == NULL) {
    return NULL;
  }
  if (name == NULL) {
    return NULL;
  }
  struct list_elem* e;
  for (e = list_begin(fdlist); e != list_end(fdlist); e = list_next(e)) {
    struct file_descriptor* curr_fd = list_entry(e, struct file_descriptor, elem);
    if (strcmp(curr_fd->file_name, name) == 0) {
      return curr_fd;
    }
  }
  return NULL;
}

//add a fd to a fd table
file_descriptor_t* add_fd(file_descriptor_list_t* fdlist, int fd, struct file* curfile,
                          const char* name) {
  /* TODO */
  if (fdlist == NULL) {
    return NULL;
  }
  if (curfile == NULL) {
    return NULL;
  }
  file_descriptor_t* new_elem = (file_descriptor_t*)malloc(sizeof(file_descriptor_t));
  if (new_elem == NULL) {
    return NULL;
  }
  new_elem->fd = fd;

  new_elem->file_name = (const char*)malloc(sizeof(char) * (strlen(name) + 1));
  strlcpy((char*)new_elem->file_name, name, strlen(name) + 1);

  //POSSIBLE ERROR: Don't know if we have to malloc a new file* pointer or if we can just set it like this.
  //If we are erroring because we cant open a file, we probably need to deep copy the struct elements.

  new_elem->open_file = curfile;

  list_insert(list_end(fdlist), &(new_elem->elem));
  return new_elem;
}

//The logic for the close syscall implmented here so that process_exit has access to it
int process_file_close(int args) {
  struct thread* t = thread_current();

  //grab the filesystem lock
  lock_acquire(t->pcb->filesys_lock);

  //Get file descriptor
  int fd = args;

  //Get the fd table entry for the fd
  file_descriptor_t* found = find_fd(t->pcb->file_descriptor_table, fd);

  //Check if fd is a valid file descriptor, and closing/freeing it if so
  if (found == NULL) {
    lock_release(t->pcb->filesys_lock);
    return (-1);
  } else {
    //close the file
    file_close(found->open_file);
    //free the file name pointer
    free((void*)found->file_name);
    //remove the table element from the list
    list_remove(&(found->elem));
    //free the table element from the heap
    free((void*)found);
    lock_release(t->pcb->filesys_lock);
    return (0);
  }
}

/* Initializes user programs in the system by ensuring the main
   thread has a minimal PCB so that it can execute and wait for
   the first user process. Any additions to the PCB should be also
   initialized here if main needs those members */
void userprog_init(void) {
  struct thread* t = thread_current();
  bool success;

  /* Allocate process control block
     It is imoprtant that this is a call to calloc and not malloc,
     so that t->pcb->pagedir is guaranteed to be NULL (the kernel's
     page directory) when t->pcb is assigned, because a timer interrupt
     can come at any time and activate our pagedir */
  t->pcb = calloc(sizeof(struct process), 1);
  success = t->pcb != NULL;

  if (success) {
    //Allocating the file_descriptor_table pointer in the the pcb
    t->pcb->file_descriptor_table = (file_descriptor_list_t*)malloc(sizeof(file_descriptor_list_t));

    //initalize the file_descriptor_table list
    init_fdtable(t->pcb->file_descriptor_table);

    //Set the file descriptor couinter
    t->pcb->fd_next = 2;

    //Allocating for child list
    t->pcb->child = (metadata_list_t*)malloc(sizeof(metadata_list_t));

    //Initalizing the child list for the main thread
    list_init(t->pcb->child);

    //Initalizing the global filesystem lock and putting it in the pcb
    lock_init(&filesys_lock);
    t->pcb->filesys_lock = &filesys_lock;
    //mallocing sharedata structure for main thread
    t->shared = (metadata_t*)malloc(sizeof(metadata_t));

    // initialising ref_cnt to 1 since main thread won't have a parent
    //initialising rest of metadta struct elements
    t->shared->ref_cnt = 1;
    t->shared->tid = t->tid;
    lock_init(&(t->shared->tlock));
    sema_init(&(t->shared->finish), 0);
    t->shared->load_check = false;
  }
  /* Kill the kernel if we did not succeed */
  ASSERT(success);
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   process id, or TID_ERROR if the thread cannot be created. */
pid_t process_execute(const char* file_name) {
  char* fn_copy;
  tid_t tid;
  struct thread* t = thread_current();

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page(0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy(fn_copy, file_name, PGSIZE);
  //malloc space for child's metadata
  metadata_t* data = (metadata_t*)malloc(sizeof(metadata_t));
  //set copy of cmd_line to metadata struct's element
  data->file_copy = fn_copy;
  //initiliase child's load semaphore which waits till it's attempted to load
  sema_init(&(data->load), 0);
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create(file_name, PRI_DEFAULT, start_process, (void*)data);
  //call down on child's load semaphore to wait till it's attempted to load
  sema_down(&(data->load));
  if (data->load_check && tid != TID_ERROR) {
    //if loaded succesfull add child process to parent's children list and return tid
    list_insert(list_end(t->pcb->child), &(data->elem));
    return tid;
  } else {
    //if unsuccesful free malloced metadata and free copy of cmd_line char pointer
    free(data);
    palloc_free_page(fn_copy);
    return -1;
  }
}

/* A thread function that loads a user process and starts it
   running. */
static void start_process(void* newmetadata) {
  //cast passed void pointer to a metadata struct
  metadata_t* data = (metadata_t*)newmetadata;
  //save passed in cmd_line in metadata to file_name
  char* file_name = (char*)data->file_copy;
  struct thread* t = thread_current();
  struct intr_frame if_;
  bool success, pcb_success;

  /* Allocate process control block */
  struct process* new_pcb = malloc(sizeof(struct process));
  success = pcb_success = new_pcb != NULL;

  /* Initialize process control block */
  if (success) {
    // Ensure that timer_interrupt() -> schedule() -> process_activate()
    // does not try to activate our uninitialized pagedir
    new_pcb->pagedir = NULL;
    t->pcb = new_pcb;

    // Continue initializing the PCB as normal
    t->pcb->main_thread = t;
    strlcpy(t->pcb->process_name, t->name, sizeof t->name);

    //Allocating the file_descriptor_table pointer in the the pcb
    t->pcb->file_descriptor_table = (file_descriptor_list_t*)malloc(sizeof(file_descriptor_list_t));

    //initalize the file_descriptor_table list
    init_fdtable(t->pcb->file_descriptor_table);

    //Set the file descriptor couinter
    t->pcb->fd_next = 2;

    //Allocating for child list
    t->pcb->child = (metadata_list_t*)malloc(sizeof(metadata_list_t));

    //initalize the child table
    list_init(t->pcb->child);

    //Puting the shared data struct in the child
    t->shared = data;

    //Initalizing the global filesystem lock and putting it in the pcb
    lock_init(&filesys_lock);
    t->pcb->filesys_lock = &filesys_lock;

    //initialising shared struct's elements
    t->shared->ref_cnt = 2;
    t->shared->tid = t->tid;
    lock_init(&(t->shared->tlock));
    sema_init(&(t->shared->finish), 0);
    t->shared->load_check = false;
  }

  /* Initialize interrupt frame and load executable. */
  if (success) {
    memset(&if_, 0, sizeof if_);
    if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
    if_.cs = SEL_UCSEG;
    if_.eflags = FLAG_IF | FLAG_MBS;
    /* Need to save the current FPU State. */
    asm volatile("fsave %0" : "=m"(if_.fpu));
    success = load(file_name, &if_.eip, &if_.esp);
  }

  /* Handle failure with succesful PCB malloc. Must free the PCB */
  if (!success && pcb_success) {
    // Avoid race where PCB is freed before t->pcb is set to NULL
    // If this happens, then an unfortuantely timed timer interrupt
    // can try to activate the pagedir, but it is now freed memory
    struct process* pcb_to_free = t->pcb;
    //close the executable before we free the pcb
    file_close(t->pcb->executable);
    //free malloced space for child list if load fails
    free(t->pcb->child);
    //free malloced file descriptor table
    free(t->pcb->file_descriptor_table);
    t->pcb = NULL;
    free(pcb_to_free);
  }

  if (!success) {
    //let waiting parent know child is finished because of unsuccessful load
    sema_up(&(t->shared->finish));
    thread_exit();
  }

  /* Clean up. Exit on failure or jump to userspace */
  palloc_free_page(file_name);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED();
}

/* Waits for process with PID child_pid to die and returns its exit status.
   If it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If child_pid is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given PID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int process_wait(pid_t child_pid) {
  struct thread* t = thread_current();
  struct list_elem* e;
  //iterate through child list and search for child with a given pid
  if (!list_empty(t->pcb->child)) {
    for (e = list_begin(t->pcb->child); e != list_end(t->pcb->child); e = list_next(e)) {
      metadata_t* cur_md = list_entry(e, metadata_t, elem);
      if (cur_md->tid == child_pid) {
        //wait till child has finished by downing finish semaphore
        sema_down(&(cur_md->finish));
        //store exitcode from child's metadata
        int exit = cur_md->exitcode;
        //acquire lock to decrement ref_cnt to avoid race conditions
        lock_acquire(&(cur_md->tlock));
        cur_md->ref_cnt -= 1;
        int temp = cur_md->ref_cnt;
        lock_release(&(cur_md->tlock));
        //remove waited child from parent's child list
        list_remove(e);
        //if ref_cnt is 0, free metadata
        if (temp == 0) {
          free(cur_md);
        }
        if (exit == -2) {
          //since we have a unique exit code for errors, we want to ensure that if -2 is returned, we turn it into -1 so it is handled properly.
          return -1;
        } else {
          return exit;
        }
      }
    }
  }
  return -1;
}

/* Free the current process's resources. */
//Changed process exit to take an exit code as an input
void process_exit(int exit_code) {
  struct thread* cur = thread_current();
  uint32_t* pd;
  //store exit code in thread's metadata
  cur->shared->exitcode = exit_code;

  /* If this thread does not have a PCB, don't worry */
  if (cur->pcb == NULL) {
    thread_exit();
    NOT_REACHED();
  } else if (exit_code == -2) {
    //printing the exit code if we exited due to an error. Set to -2 because only excpetion should return
    //an exit code of -2
    printf("%s: exit(%d)\n", thread_current()->pcb->process_name, exit_code + 1);
  }

  //close the executable before we free the pcb
  file_close(cur->pcb->executable);

  //Iterate through the file descriptor list and close all of the file descriptors that are still open
  struct list_elem* e;
  while (!list_empty(cur->pcb->file_descriptor_table)) {
    e = list_begin(cur->pcb->file_descriptor_table);
    file_descriptor_t* fd_entry = list_entry(e, file_descriptor_t, elem);
    process_file_close(fd_entry->fd);
  }
  //free the file descriptor list from the heap
  free((void*)cur->pcb->file_descriptor_table);

  //acquire lock to decrement ref_cnt to avoid race conditions and free if 0
  lock_acquire(&(cur->shared->tlock));
  cur->shared->ref_cnt -= 1;
  int temp = cur->shared->ref_cnt;
  lock_release(&(cur->shared->tlock));

  if (temp == 0) {
    free(cur->shared);
  }

  //iterate through all children and decrement their shared data's ref_cnt
  struct list_elem* temp_next;
  if (!list_empty(cur->pcb->child)) {
    for (e = list_begin(cur->pcb->child); e != list_end(cur->pcb->child);) {
      metadata_t* cur_md = list_entry(e, metadata_t, elem);
      //acquire lock to decrement ref_cnt to avoid race conditions and free if 0
      lock_acquire(&(cur_md->tlock));
      cur_md->ref_cnt -= 1;
      int temp = cur_md->ref_cnt;
      lock_release(&(cur_md->tlock));
      temp_next = list_next(e);
      if (temp == 0) {
        free(cur_md);
      }
      e = temp_next;
    }
  }
  //free threads list struct of children
  free((void*)cur->pcb->child);

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pcb->pagedir;
  if (pd != NULL) {
    /* Correct ordering here is crucial.  We must set
         cur->pcb->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
    cur->pcb->pagedir = NULL;
    pagedir_activate(NULL);
    pagedir_destroy(pd);
  }

  /* Free the PCB of this process and kill this thread
     Avoid race where PCB is freed before t->pcb is set to NULL
     If this happens, then an unfortuantely timed timer interrupt
     can try to activate the pagedir, but it is now freed memory */
  struct process* pcb_to_free = cur->pcb;
  cur->pcb = NULL;
  free(pcb_to_free);
  //up on finish semaphore so waiting parent can continue
  sema_up(&(cur->shared->finish));

  thread_exit();
}

/* Sets up the CPU for running user code in the current
   thread. This function is called on every context switch. */
void process_activate(void) {
  struct thread* t = thread_current();

  /* Activate thread's page tables. */
  if (t->pcb != NULL && t->pcb->pagedir != NULL)
    pagedir_activate(t->pcb->pagedir);
  else
    pagedir_activate(NULL);

  /* Set thread's kernel stack for use in processing interrupts.
     This does nothing if this is not a user process. */
  tss_update();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32 /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr {
  unsigned char e_ident[16];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr {
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL 0           /* Ignore. */
#define PT_LOAD 1           /* Loadable segment. */
#define PT_DYNAMIC 2        /* Dynamic linking info. */
#define PT_INTERP 3         /* Name of dynamic loader. */
#define PT_NOTE 4           /* Auxiliary info. */
#define PT_SHLIB 5          /* Reserved. */
#define PT_PHDR 6           /* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

static bool setup_stack(void** esp);
static bool validate_segment(const struct Elf32_Phdr*, struct file*);
static bool load_segment(struct file* file, off_t ofs, uint8_t* upage, uint32_t read_bytes,
                         uint32_t zero_bytes, bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool load(const char* file_name, void (**eip)(void), void** esp) {
  struct thread* t = thread_current();
  struct Elf32_Ehdr ehdr;
  struct file* file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  //___START____: Code By Shaamer For Argument Passing PART 1/2

  //Make a copy of the entire command line
  char* file_copy = malloc(sizeof(char) * (strlen(file_name) + 1));
  strlcpy(file_copy, file_name, strlen(file_name) + 1);

  //Calculate the number of args
  int numspaces = 0;

  //Boolean protection against double+ spaces
  bool last = false;

  //Loop to count the number of spces = args -1
  for (unsigned i = 0; i < strlen(file_name); i++) {
    if (file_name[i] == ' ') {
      if (!last) {
        numspaces += 1;
        last = true;
      }
    } else {
      last = false;
    }
  }

  //Number of args
  int argc = numspaces + 1;

  //Creating the argv array
  char* argv[argc + 1];

  //Set the last element of argv to Null
  argv[argc] = NULL;

  //Counter to access argv array
  int counter = 0;

  // How to use Strtok_r on https://www.geeksforgeeks.org/strtok-strtok_r-functions-c-examples/
  char* saveptr = NULL;
  for (char* token_elem = strtok_r(file_copy, " ", &saveptr); token_elem != NULL;
       token_elem = strtok_r(NULL, " ", &saveptr)) {

    // Parse file, store in argv
    int len = strlen(token_elem);
    argv[counter] = (char*)malloc(sizeof(char) * (len + 1));
    strlcpy(argv[counter], token_elem, len + 1);
    counter++;
  }

  //Check if there are no arguments
  if (argc == 0) {
    //we passed in something empty
    success = false;
    printf("%s/n", "Didn't pass in anything");
    return success;
  }

  //Set the process name to be the name of the test/executable
  strlcpy(t->pcb->process_name, argv[0], strlen(argv[0]) + 1);

  /* PUSHING TO STACK IMPLEMENTED BELOW IN PART 2.*/

  //___END____: Code By Shaamer For Argument Passing PART 1/2

  /* Allocate and activate page directory. */
  t->pcb->pagedir = pagedir_create();
  if (t->pcb->pagedir == NULL)
    goto done;
  process_activate();

  /* Open executable file. */
  //SHAAMER CHANGED THE INPUT TO FILESYS_OPEN BELOW from file_name to argv[0]
  file = filesys_open(argv[0]);
  if (file == NULL) {
    printf("load: %s: open failed\n", file_name);
    goto done;
    //putting the executable into the pcb so we can close it in process exit
  } else {
    t->pcb->executable = file;
    file_deny_write(file);
  }

  /* Read and verify executable header. */
  if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr ||
      memcmp(ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2 || ehdr.e_machine != 3 ||
      ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Elf32_Phdr) || ehdr.e_phnum > 1024) {
    printf("load: %s: error loading executable\n", file_name);
    goto done;
  }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) {
    struct Elf32_Phdr phdr;

    if (file_ofs < 0 || file_ofs > file_length(file))
      goto done;
    file_seek(file, file_ofs);

    if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
      goto done;
    file_ofs += sizeof phdr;
    switch (phdr.p_type) {
      case PT_NULL:
      case PT_NOTE:
      case PT_PHDR:
      case PT_STACK:
      default:
        /* Ignore this segment. */
        break;
      case PT_DYNAMIC:
      case PT_INTERP:
      case PT_SHLIB:
        goto done;
      case PT_LOAD:
        if (validate_segment(&phdr, file)) {
          bool writable = (phdr.p_flags & PF_W) != 0;
          uint32_t file_page = phdr.p_offset & ~PGMASK;
          uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
          uint32_t page_offset = phdr.p_vaddr & PGMASK;
          uint32_t read_bytes, zero_bytes;
          if (phdr.p_filesz > 0) {
            /* Normal segment.
                     Read initial part from disk and zero the rest. */
            read_bytes = page_offset + phdr.p_filesz;
            zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
          } else {
            /* Entirely zero.
                     Don't read anything from disk. */
            read_bytes = 0;
            zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
          }
          if (!load_segment(file, file_page, (void*)mem_page, read_bytes, zero_bytes, writable))
            goto done;
        } else
          goto done;
        break;
    }
  }

  /* Set up stack. */
  if (!setup_stack(esp))
    goto done;

  /* Start address. */
  *eip = (void (*)(void))ehdr.e_entry;

  //___START____: Code By Shaamer For Argument Passing PART 2/2

  //Push the actual arguments onto the stack in reverse order
  for (int k = argc - 1; k >= 0; k--) {
    *esp -= strlen(argv[k]) + 1;
    memcpy(*esp, argv[k], strlen(argv[k]) + 1);
    argv[k] = *esp;
  }
  /*START STACK ALIGN*/
  // arg pointers + nullpointer + **argv + argc
  int numpointers = 3 + argc;
  int amount_to_decrement = ((unsigned)*esp - numpointers * 4) % 16;
  *esp -= amount_to_decrement;
  /*END STACK ALIGN*/

  /*push addresses of arguments in reverse order*/
  for (int k = argc; k >= 0; k--) {
    *esp -= 4;
    *(uint32_t*)*esp = (int)(uint32_t*)argv[k];
  }

  /*push **argv */
  *esp -= 4;
  *(uint32_t*)*esp = (int)(*esp + 4);

  /*push argc */
  *esp -= 4;
  *(uint32_t*)*esp = argc;

  /*push fake return address */
  *esp -= 4;
  *(uint32_t*)*esp = 0;

  //___END____: Code By Shaamer For Argument Passing PART 2/2

  success = true;

done:
  /* We arrive here whether the load is successful or not. */
  //Commenting this out here so we can close the executable in process exit and prevent writing to the executable
  //during the entire process
  //file_close(file);

  //Free resources if the load fails
  if (!success) {
    free(file_copy);
    for (int i = 0; i < counter; i++) {
      free(argv[i]);
    }
  }

  //store loads success status in the threads metadata
  t->shared->load_check = success;
  //up load semaphore for process_execute to continue executing
  sema_up(&(t->shared->load));
  return success;
}

/* load() helpers. */

static bool install_page(void* upage, void* kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool validate_segment(const struct Elf32_Phdr* phdr, struct file* file) {
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off)file_length(file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr((void*)phdr->p_vaddr))
    return false;
  if (!is_user_vaddr((void*)(phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool load_segment(struct file* file, off_t ofs, uint8_t* upage, uint32_t read_bytes,
                         uint32_t zero_bytes, bool writable) {
  ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT(pg_ofs(upage) == 0);
  ASSERT(ofs % PGSIZE == 0);

  file_seek(file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) {
    /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    /* Get a page of memory. */
    uint8_t* kpage = palloc_get_page(PAL_USER);
    if (kpage == NULL)
      return false;

    /* Load this page. */
    if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes) {
      palloc_free_page(kpage);
      return false;
    }
    memset(kpage + page_read_bytes, 0, page_zero_bytes);

    /* Add the page to the process's address space. */
    if (!install_page(upage, kpage, writable)) {
      palloc_free_page(kpage);
      return false;
    }

    /* Advance. */
    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    upage += PGSIZE;
  }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool setup_stack(void** esp) {
  uint8_t* kpage;
  bool success = false;

  kpage = palloc_get_page(PAL_USER | PAL_ZERO);
  if (kpage != NULL) {
    success = install_page(((uint8_t*)PHYS_BASE) - PGSIZE, kpage, true);
    if (success)
      *esp = PHYS_BASE;
    else
      palloc_free_page(kpage);
  }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool install_page(void* upage, void* kpage, bool writable) {
  struct thread* t = thread_current();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page(t->pcb->pagedir, upage) == NULL &&
          pagedir_set_page(t->pcb->pagedir, upage, kpage, writable));
}

/* Returns true if t is the main thread of the process p */
bool is_main_thread(struct thread* t, struct process* p) { return p->main_thread == t; }

/* Gets the PID of a process */
pid_t get_pid(struct process* p) { return (pid_t)p->main_thread->tid; }

/* Creates a new stack for the thread and sets up its arguments.
   Stores the thread's entry point into *EIP and its initial stack
   pointer into *ESP. Handles all cleanup if unsuccessful. Returns
   true if successful, false otherwise.

   This function will be implemented in Project 2: Multithreading. For
   now, it does nothing. You may find it necessary to change the
   function signature. */
bool setup_thread(void (**eip)(void) UNUSED, void** esp UNUSED) { return false; }

/* Starts a new thread with a new user stack running SF, which takes
   TF and ARG as arguments on its user stack. This new thread may be
   scheduled (and may even exit) before pthread_execute () returns.
   Returns the new thread's TID or TID_ERROR if the thread cannot
   be created properly.

   This function will be implemented in Project 2: Multithreading and
   should be similar to process_execute (). For now, it does nothing.
   */
tid_t pthread_execute(stub_fun sf UNUSED, pthread_fun tf UNUSED, void* arg UNUSED) { return -1; }

/* A thread function that creates a new user thread and starts it
   running. Responsible for adding itself to the list of threads in
   the PCB.

   This function will be implemented in Project 2: Multithreading and
   should be similar to start_process (). For now, it does nothing. */
static void start_pthread(void* exec_ UNUSED) {}

/* Waits for thread with TID to die, if that thread was spawned
   in the same process and has not been waited on yet. Returns TID on
   success and returns TID_ERROR on failure immediately, without
   waiting.

   This function will be implemented in Project 2: Multithreading. For
   now, it does nothing. */
tid_t pthread_join(tid_t tid UNUSED) { return -1; }

/* Free the current thread's resources. Most resources will
   be freed on thread_exit(), so all we have to do is deallocate the
   thread's userspace stack. Wake any waiters on this thread.

   The main thread should not use this function. See
   pthread_exit_main() below.

   This function will be implemented in Project 2: Multithreading. For
   now, it does nothing. */
void pthread_exit(void) {}

/* Only to be used when the main thread explicitly calls pthread_exit.
   The main thread should wait on all threads in the process to
   terminate properly, before exiting itself. When it exits itself, it
   must terminate the process in addition to all necessary duties in
   pthread_exit.

   This function will be implemented in Project 2: Multithreading. For
   now, it does nothing. */
void pthread_exit_main(void) {}
