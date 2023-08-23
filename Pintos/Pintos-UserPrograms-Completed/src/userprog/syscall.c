#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"

//Including other files so we can use them in pointer validation and the syscalls
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

//Include the locking library
#include "threads/synch.h"

//Including so we can free and string operations
#include <stdlib.h>
#include <string.h>

//Defining functions that will be called in this file
static void syscall_handler(struct intr_frame*);
int input_getc(void);
void free(void* __ptr);
int shutdown_power_off(void);
int sys_sum_to_e(int n);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

//Function to check if a pointer passed into the kernel is valid
bool is_valid_ptr(char* ptr) {
  struct thread* t = thread_current();

  //Checking if the pointer is null
  if (ptr == NULL) {
    return false;
  }
  //checking if the entire memory address is in userspace
  if (!is_user_vaddr(ptr) || !is_user_vaddr(ptr + 3)) {
    return false;
  }

  //Checking if the entire memory address is in mapped memory
  if (pagedir_get_page(t->pcb->pagedir, ptr) == NULL ||
      pagedir_get_page(t->pcb->pagedir, (ptr + 3)) == NULL) {
    return false;
  }
  return true;
}

//Funtion to check if integers passed into the kernel are valid
bool is_valid_int(uint32_t* ptr) {
  return is_valid_ptr(((char*)ptr)) && is_valid_ptr(((char*)ptr) + 3);
}

static void syscall_handler(struct intr_frame* f UNUSED) {
  //This is the exit code we have set for if we are exiting due to an exception
  //Checking if we have a valid stack pointer
  if (!is_valid_ptr((char*)f->esp)) {
    process_exit(-2);
  }

  uint32_t* args = ((uint32_t*)f->esp);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  //Checking if arg[0] is in a valid part of memory
  if (!is_valid_int(&args[0])) {
    //Exit as an exception/error
    process_exit(-2);
  }

  if (args[0] == SYS_EXIT) {
    //making sure that the integer argument is valid
    if (!is_valid_int(&args[1])) {
      //Exit as an exception/error
      process_exit(-2);
    }
    //Set return value to be the exit code
    f->eax = args[1];
    printf("%s: exit(%d)\n", thread_current()->pcb->process_name, args[1]);
    process_exit(args[1]);
  } else if (args[0] == SYS_EXEC) {
    //check if the pointers are valid
    if (!is_valid_ptr((char*)args[1])) {
      //Exit as an exception/error
      process_exit(-2);
    }
    //store cmd_line into a variable to pass to process execute
    const char* cmnd_line = (const char*)args[1];
    //store pid returned by process_execute in the eax and return
    f->eax = process_execute(cmnd_line);
    return;
  } else if (args[0] == SYS_WAIT) {
    if (!is_valid_int(&args[1])) {
      //Exit as an exception/error
      process_exit(-2);
    }
    //store exitcode returned by process_wait in the eax and return
    f->eax = process_wait((pid_t)args[1]);
    return;
  } else if (args[0] == SYS_WRITE) {
    //Obtain the current thread so we have access to the pcb
    struct thread* t = thread_current();

    //grab the global filesystem lock
    lock_acquire(t->pcb->filesys_lock);

    //check if the pointers are valid
    if (!is_valid_ptr((char*)args[2]) || !is_valid_int(&args[1]) || !is_valid_int(&args[3])) {
      //release the filesystem lock and exit
      lock_release(t->pcb->filesys_lock);

      //Exit as an exception/error
      process_exit(-2);
    }

    //get the file descriptor entered into the system call
    int fd = args[1];

    //get the buffer pointer for the write
    void* buffer = (void*)args[2];

    //get the amount we are going to write
    //POSSIBLE ERROR: Do not know if we need to check if this equals zero
    unsigned size = args[3];

    //Check if the file descriptor is valid. cannot write to stdin
    if (fd <= 0) {
      //set return value
      f->eax = -1;
      //release the filesystem lock
      lock_release(t->pcb->filesys_lock);
      return;
    }

    //Check if we are writing to standard out
    if (fd == 1) {

      if (size <= 200) {
        // Write the whole buffer in one call to putbuf
        putbuf(buffer, size);
      } else {
        // Break up the buffer into smaller chunks and write each chunck until we have written the entire thing
        unsigned offset = 0;
        while (offset < size) {
          int chunk_size = size - offset;
          if (chunk_size > 200) {
            chunk_size = 200;
          }
          putbuf(buffer + offset, chunk_size);
          offset += chunk_size;
        }
      }

      //Set return value
      f->eax = size;

      //release the filesystem lock
      lock_release(t->pcb->filesys_lock);
      return;

      //Else we are writing to a file descriptor
    } else {

      //Get the entry in the file descriptor table and check if it exists
      file_descriptor_t* table_entry = find_fd(t->pcb->file_descriptor_table, fd);
      if (table_entry == NULL) {
        //set the return value
        f->eax = -1;
        //release the filesystem lock
        lock_release(t->pcb->filesys_lock);
        return;
      }

      //get the open file from the table entry
      struct file* open_file = table_entry->open_file;

      //variable to hold how many bytes were written
      int bytes_written = 0;
      if (size <= 200) {
        // Write the whole buffer in one call to file_write
        bytes_written = file_write(open_file, buffer, size);
      } else {
        // Break up the buffer into smaller chunks and iteratively write until we we are done
        unsigned offset = 0;
        while (offset < size) {
          int chunk_size = size - offset;
          if (chunk_size > 200) {
            chunk_size = 200;
          }
          bytes_written += file_write(open_file, buffer + offset, chunk_size);
          offset += chunk_size;
        }
      }

      //set the return value
      f->eax = bytes_written;

      //release the filesystem lock
      lock_release(t->pcb->filesys_lock);

      return;
    }
  }
  //Implementation for Practice Syscall
  else if (args[0] == SYS_PRACTICE) {
    // increment argument by 1 and store in eax
    f->eax = args[1] + 1;
    return;
  }
  //Implementation for Halt Syscall
  else if (args[0] == SYS_HALT) {
    // call shutdown_power_off()
    shutdown_power_off();
  }
  if (args[0] == SYS_CREATE) {
    //Obtain the current thread so we have access to the pcb
    struct thread* t = thread_current();

    //grab the filesystem lock
    lock_acquire(t->pcb->filesys_lock);

    //Check if all the pointers are valid
    if (!is_valid_ptr((char*)args[1]) || !is_valid_int(&args[2])) {
      //release the filesystem lock and exit as an error
      lock_release(t->pcb->filesys_lock);
      process_exit(-2);
    }

    //get the file* pointer and cast it
    const char* file = (const char*)args[1];

    //Check if we are trying to create a file with a name that is too long
    if (strlen(file) > 14) {
      f->eax = false;
      lock_release(t->pcb->filesys_lock);
      return;
    }

    //Get the intial size of the file we are creating
    //POSSIBLE ERROR: DO NOT KNOW IF WE NEED TO CHECK IF THIS IS 0
    unsigned size = args[2];

    //Create the file
    bool success = filesys_create(file, size);

    //set return value
    f->eax = success;

    //Release the global files system lock
    lock_release(t->pcb->filesys_lock);

    //Return whether creating the file was succesful or not
    return;

  } else if (args[0] == SYS_REMOVE) {
    //Get the current thread
    struct thread* t = thread_current();

    //grab the filesystem lock
    lock_acquire(t->pcb->filesys_lock);

    //get the file* pointer check if it is valid, then cast it
    void* ptr = (void*)args[1];
    if (!is_valid_ptr((char*)ptr)) {
      //release the filesystem lock and exit
      lock_release(t->pcb->filesys_lock);
      process_exit(-2);
    }
    const char* file = (const char*)ptr;

    //Remove the file
    bool success = filesys_remove(file);

    //Set return value;
    f->eax = success;

    //Release the global files system lock
    lock_release(t->pcb->filesys_lock);
    return;

  } else if (args[0] == SYS_OPEN) {

    struct thread* t = thread_current();

    //grab the filesystem lock
    lock_acquire(t->pcb->filesys_lock);

    //get the file* pointer check if it is valid, then cast it
    void* ptr = (void*)args[1];
    if (!is_valid_ptr((char*)ptr)) {
      //release the filesystem lock and exit as an error
      lock_release(t->pcb->filesys_lock);
      process_exit(-2);
    }
    const char* file = (const char*)ptr;

    //Check if we already have an open file with the same file name
    file_descriptor_t* exists = find_filename(t->pcb->file_descriptor_table, file);

    //Initalizing the new file descriptor return value
    int newfd;

    //If we are not reopening a file
    if (exists == NULL) {
      //Get the new fd
      newfd = t->pcb->fd_next;
      //increment the fd counter so next call to open will return a different fd
      t->pcb->fd_next++;
      //open the file with the file name
      struct file* open_file = filesys_open(file);
      //check if the open was succseful
      if (open_file == NULL) {
        f->eax = -1;
        lock_release(t->pcb->filesys_lock);
        return;
      }
      //add the new file and fd to the table
      add_fd(t->pcb->file_descriptor_table, newfd, open_file, file);
      //if we are reopening a file
    } else {
      //Get the new fd
      newfd = t->pcb->fd_next;
      //increment the fd counter so next call to open will return a different fd
      t->pcb->fd_next++;
      //reopen the file so we get a new position
      struct file* open_file = file_reopen(exists->open_file);
      //check if the open was succseful
      if (open_file == NULL) {
        f->eax = -1;
        lock_release(t->pcb->filesys_lock);
        return;
      }
      //add the new file and fd to the table
      add_fd(t->pcb->file_descriptor_table, newfd, open_file, file);
    }

    f->eax = newfd;

    //Release the global file system lock
    lock_release(t->pcb->filesys_lock);

    return;

  } else if (args[0] == SYS_SEEK) {
    //Get current running thread
    struct thread* t = thread_current();

    //grab the filesystem lock
    lock_acquire(t->pcb->filesys_lock);

    //Check if the arg pointers are valid
    if (!is_valid_int(&args[1]) || !is_valid_int(&args[2])) {
      //release the filesystem lock and exit as an error
      lock_release(t->pcb->filesys_lock);
      process_exit(-2);
    }

    //Get file descriptor
    int fd = args[1];

    //Get the fd table entry for the fd
    file_descriptor_t* found = find_fd(t->pcb->file_descriptor_table, fd);

    //Check whether the fd exists
    if (found == NULL) {
      //Release the lock and return
      lock_release(t->pcb->filesys_lock);
      return;
    } else {
      //Get how many bytes we are seeking
      //POSSIBLE ERROR: DONT KNOW IF WE NEED TO CHECK IF SIZE = 0;
      unsigned size = args[2];
      //call seek on the file to change its position
      file_seek(found->open_file, size);
      //release the file system lock
      lock_release(t->pcb->filesys_lock);
      return;
    }
  } else if (args[0] == SYS_TELL) {
    //Get current running thread
    struct thread* t = thread_current();

    //grab the filesystem lock
    lock_acquire(t->pcb->filesys_lock);

    //Check if the arg pointers are valid
    if (!is_valid_int(&args[1])) {
      //release the filesystem lock and exit as an error
      lock_release(t->pcb->filesys_lock);
      process_exit(-2);
    }

    //Get file descriptor
    int fd = args[1];

    //Get the fd table entry for the fd
    file_descriptor_t* found = find_fd(t->pcb->file_descriptor_table, fd);

    //Check if fd is a valid file descriptor, and return its offset if so
    if (found == NULL) {
      //set the return vale
      f->eax = -1;
      //release the file system lock
      lock_release(t->pcb->filesys_lock);
      return;
    } else {
      //find the next byte to read
      off_t tell = file_tell(found->open_file);
      //set the return value
      f->eax = tell;
      //release the file system lock
      lock_release(t->pcb->filesys_lock);
      return;
    }
  } else if (args[0] == SYS_CLOSE) {
    //Check if the arg pointers are valid
    if (!is_valid_int(&args[1])) {
      process_exit(-2);
    }
    //Create a process close function in order to be able to use the close logic in process exit as well
    f->eax = process_file_close((int)args[1]);
    return;
  } else if (args[0] == SYS_FILESIZE) {
    //Get current running thread
    struct thread* t = thread_current();

    //grab the filesystem lock
    lock_acquire(t->pcb->filesys_lock);

    //Check if the arg pointers are valid
    if (!is_valid_int(&args[1])) {
      //release the filesystem lock and exit as an error
      lock_release(t->pcb->filesys_lock);
      process_exit(-2);
    }

    //Get file descriptor
    int fd = args[1];

    //Get the fd table entry for the fd
    file_descriptor_t* found = find_fd(t->pcb->file_descriptor_table, fd);

    //Check if fd is a valid file descriptor, and returning its offset if so
    if (found == NULL) {
      //set the return value
      f->eax = -1;
      //release the file system lock
      lock_release(t->pcb->filesys_lock);
      return;
    } else {
      //Get the file size
      off_t len = file_length(found->open_file);
      //set the return value
      f->eax = len;
      //release the file system lock and return
      lock_release(t->pcb->filesys_lock);
      return;
    }
  } else if (args[0] == SYS_READ) {
    //Obtain the current thread so we have access to the pcb
    struct thread* t = thread_current();

    //grab the filesystem lock
    lock_acquire(t->pcb->filesys_lock);

    //check if the pointers are valid
    if (!is_valid_ptr((char*)args[2]) || !is_valid_int(&args[1]) || !is_valid_int(&args[3])) {
      //release the filesystem lock and exit as an error
      lock_release(t->pcb->filesys_lock);
      process_exit(-2);
    }

    //get the file descriptor entered into the system call
    int fd = args[1];

    //get the amount we are going to read
    //POSSIBLE ERROR: Do not know if we need to check if this equals zero
    unsigned size = args[3];

    //Check if the file descriptor is valid. cannot read from stdout
    if (fd == 1 || fd < 0) {
      //set the return value
      f->eax = -1;
      //release the filesystem lock and return
      lock_release(t->pcb->filesys_lock);
      return;
    }

    //Check if we are reading from stdin
    if (fd == 0) {
      //Get the buffer and set it to be a char* since we are indexing it
      char* buffer = (char*)args[2];
      for (unsigned i = 0; i < size; i++) {
        // Read a character from the keyboard
        //POSSIBLE ERROR: DO WE NEED TO STOP IF WE HIT A \n
        // Store the character in the buffer
        buffer[i] = input_getc();
      }
      //set the return value
      f->eax = size;
      //release the lock and return
      lock_release(t->pcb->filesys_lock);
      return;
      //we are reading from a file descriptor we put in the table
    } else {
      //Get the buffer
      void* buffer = (void*)args[2];

      //Get the entry in the file descriptor table and check if it exists
      file_descriptor_t* table_entry = find_fd(t->pcb->file_descriptor_table, fd);
      if (table_entry == NULL) {
        //set the return value
        f->eax = -1;
        //release the filesystem lock and return
        lock_release(t->pcb->filesys_lock);
        return;
      }

      //get the open file from the table entry
      struct file* open_file = table_entry->open_file;

      //Execute the read
      int read = file_read(open_file, buffer, size);

      //Set the return value
      f->eax = read;

      //Release the lock and return
      lock_release(t->pcb->filesys_lock);

      return;
    }
    //Implementation for Compute E Syscall
  } else if (args[0] == SYS_COMPUTE_E) {
    int n = args[1];
    if (n < 0) {
      process_exit(-2);
    } else {
      f->eax = sys_sum_to_e(n);
      return;
    }
  }
}
