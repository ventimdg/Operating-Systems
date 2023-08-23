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

#include "filesys/inode.h"
#include "filesys/directory.h"

struct block {
  struct list_elem list_elem; /* Element in all_blocks. */

  char name[16];        /* Block device name. */
  enum block_type type; /* Type of block device. */
  block_sector_t size;  /* Size in sectors. */

  const struct block_operations* ops; /* Driver operations. */
  void* aux;                          /* Extra data owned by driver. */

  unsigned long long read_cnt;  /* Number of sectors read. */
  unsigned long long write_cnt; /* Number of sectors written. */
};

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

    //check if the pointers are valid
    if (!is_valid_ptr((char*)args[2]) || !is_valid_int(&args[1]) || !is_valid_int(&args[3])) {

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

      return;

      //Else we are writing to a file descriptor
    } else {

      //Get the entry in the file descriptor table and check if it exists
      file_descriptor_t* table_entry = find_fd(t->pcb->file_descriptor_table, fd);
      if (table_entry == NULL || table_entry->is_dir) {
        //set the return value
        f->eax = -1;

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

    shutdown_power_off();
  }
  if (args[0] == SYS_CREATE) {
    //Obtain the current thread so we have access to the pcb
    struct thread* t = thread_current();

    //Check if all the pointers are valid
    if (!is_valid_ptr((char*)args[1]) || !is_valid_int(&args[2])) {

      process_exit(-2);
    }

    //get the file* pointer and cast it
    const char* file = (const char*)args[1];

    //Check if we are trying to create a file with a name that is too long
    if (strlen(file) > 14) {
      f->eax = false;

      return;
    }

    if (strlen(file) == 0) {
      f->eax = false;

      return;
    }

    //Get the intial size of the file we are creating
    //POSSIBLE ERROR: DO NOT KNOW IF WE NEED TO CHECK IF THIS IS 0
    unsigned size = args[2];

    //Create the file
    bool success = filesys_create(file, size, false);

    //set return value
    f->eax = success;

    //Return whether creating the file was succesful or not
    return;

  } else if (args[0] == SYS_REMOVE) {
    //Get the current thread
    struct thread* t = thread_current();

    //get the file* pointer check if it is valid, then cast it
    void* ptr = (void*)args[1];
    if (!is_valid_ptr((char*)ptr)) {

      process_exit(-2);
    }
    const char* file = (const char*)ptr;

    //Remove the file
    bool success = filesys_remove(file);

    //Set return value;
    f->eax = success;

    return;

  } else if (args[0] == SYS_OPEN) {

    struct thread* t = thread_current();

    //get the file* pointer check if it is valid, then cast it
    void* ptr = (void*)args[1];
    if (!is_valid_ptr((char*)ptr)) {

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
      bool is_dir;

      void* open_file = filesys_open(file, &is_dir);
      //check if the open was succseful
      if (open_file == NULL) {
        f->eax = -1;

        return;
      }
      //add the new file and fd to the table
      add_fd(t->pcb->file_descriptor_table, newfd, open_file, file, is_dir);
      //if we are reopening a file
    } else {
      //Get the new fd
      newfd = t->pcb->fd_next;
      //increment the fd counter so next call to open will return a different fd
      t->pcb->fd_next++;
      //reopen the file so we get a new position

      if (exists->is_dir) {
        struct dir* open_dir = dir_reopen(exists->directory);
        //check if the open was succseful
        if (open_dir == NULL) {
          f->eax = -1;

          return;
        }
        //add the new file and fd to the table
        add_fd(t->pcb->file_descriptor_table, newfd, (void*)open_dir, file, true);
      } else {
        struct file* open_file = file_reopen(exists->open_file);
        //check if the open was succseful
        if (open_file == NULL) {
          f->eax = -1;

          return;
        }
        //add the new file and fd to the table
        add_fd(t->pcb->file_descriptor_table, newfd, (void*)open_file, file, false);
      }
    }

    f->eax = newfd;

    //Release the global file system lock

    return;

  } else if (args[0] == SYS_SEEK) {
    //Get current running thread
    struct thread* t = thread_current();

    //grab the filesystem lock

    //Check if the arg pointers are valid
    if (!is_valid_int(&args[1]) || !is_valid_int(&args[2])) {
      //release the filesystem lock and exit as an error

      process_exit(-2);
    }

    //Get file descriptor
    int fd = args[1];

    //Get the fd table entry for the fd
    file_descriptor_t* found = find_fd(t->pcb->file_descriptor_table, fd);

    //Check whether the fd exists
    if (found == NULL) {
      //Release the lock and return

      return;
    } else {
      //Get how many bytes we are seeking
      //POSSIBLE ERROR: DONT KNOW IF WE NEED TO CHECK IF SIZE = 0;
      unsigned size = args[2];
      //call seek on the file to change its position
      file_seek(found->open_file, size);
      //release the file system lock

      return;
    }
  } else if (args[0] == SYS_TELL) {
    //Get current running thread
    struct thread* t = thread_current();

    //grab the filesystem lock

    //Check if the arg pointers are valid
    if (!is_valid_int(&args[1])) {
      //release the filesystem lock and exit as an error

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

      return;
    } else {
      //find the next byte to read
      off_t tell = file_tell(found->open_file);
      //set the return value
      f->eax = tell;
      //release the file system lock

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

    //Check if the arg pointers are valid
    if (!is_valid_int(&args[1])) {
      //release the filesystem lock and exit as an error

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

      return;
    } else {
      //Get the file size
      off_t len = file_length(found->open_file);
      //set the return value
      f->eax = len;
      //release the file system lock and return

      return;
    }
  } else if (args[0] == SYS_READ) {
    //Obtain the current thread so we have access to the pcb
    struct thread* t = thread_current();

    //grab the filesystem lock

    //check if the pointers are valid
    if (!is_valid_ptr((char*)args[2]) || !is_valid_int(&args[1]) || !is_valid_int(&args[3])) {
      //release the filesystem lock and exit as an error

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

      return;
      //we are reading from a file descriptor we put in the table
    } else {
      //Get the buffer
      void* buffer = (void*)args[2];

      //Get the entry in the file descriptor table and check if it exists
      file_descriptor_t* table_entry = find_fd(t->pcb->file_descriptor_table, fd);
      if (table_entry == NULL || table_entry->is_dir) {
        //set the return value
        f->eax = -1;
        //release the filesystem lock and return

        return;
      }

      //get the open file from the table entry
      struct file* open_file = table_entry->open_file;

      //Execute the read
      int read = file_read(open_file, buffer, size);

      //Set the return value
      f->eax = read;

      //Release the lock and return

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
  } else if (args[0] == SYS_INUMBER) {

    struct thread* t = thread_current();

    if (!is_valid_int(&args[1])) {
      process_exit(-2);
    }

    int fd = args[1];

    file_descriptor_t* table_entry = find_fd(t->pcb->file_descriptor_table, fd);

    if (table_entry == NULL) {
      //set the return value
      f->eax = -1;
      //release the filesystem lock and return

      return;
    } else {

      if (table_entry->is_dir) {
        f->eax = table_entry->directory->inode->sector;
      } else {
        f->eax = table_entry->open_file->inode->sector;
      }
      return;
    }
  } else if (args[0] == SYS_CHDIR) {

    void* ptr = (void*)args[1];
    if (!is_valid_ptr((char*)ptr)) {
      //release the filesystem lock and exit as an error

      process_exit(-2);
    }

    const char* file = (const char*)ptr;

    struct thread* t = thread_current();

    block_sector_t old_cwd_sector = t->pcb->CWD;

    struct inode* inode = NULL;

    char* new_name = file;

    struct dir* dir = resolve_path(new_name);

    if (dir == NULL) {
      f->eax = false;
      return;
    }

    char part[NAME_MAX + 1];

    int holder = get_next_part(part, &new_name);

    while (holder != 0) {
      holder = get_next_part(part, &new_name);
    }

    if (dir != NULL)
      dir_lookup(dir, part, &inode);

    if (inode == NULL || !inode->is_dir) {
      dir_close(dir);
      inode_close(inode);
      f->eax = false;
      return;
    } else {

      struct inode* old_cwd = inode_open(old_cwd_sector);

      if (!old_cwd->is_root) {
        rw_lock_acquire(&old_cwd->inode_rwlock, false);
        old_cwd->ref_cwd--;
        rw_lock_release(&old_cwd->inode_rwlock, false);
      }

      inode_close(old_cwd);

      if (!inode->is_root) {
        rw_lock_acquire(&inode->inode_rwlock, false);
        inode->ref_cwd++;
        rw_lock_release(&inode->inode_rwlock, false);
      }

      t->pcb->CWD = inode->sector;
      inode_close(inode);
      dir_close(dir);
      f->eax = true;
      return;
    }
  }

  else if (args[0] == SYS_MKDIR) {

    void* ptr = (void*)args[1];
    if (!is_valid_ptr((char*)ptr)) {
      //release the filesystem lock and exit as an error

      process_exit(-2);
    }

    char* file = (char*)ptr;

    if (strlen(file) == 0) {
      f->eax = false;
      return;
    }

    f->eax = filesys_create(file, 0, true);
    return;

  } else if (args[0] == SYS_READDIR) {

    if (!is_valid_ptr((char*)args[2]) || !is_valid_int(&args[1])) {

      process_exit(-2);
    }

    struct thread* t = thread_current();

    char* file = (char*)args[2];
    int fd = (int)args[1];

    file_descriptor_t* table_entry = find_fd(t->pcb->file_descriptor_table, fd);

    if (table_entry->is_dir) {
      f->eax = dir_readdir(table_entry->directory, file);
      return;
    } else {
      f->eax = false;
      return;
    }
  } else if (args[0] == SYS_ISDIR) {

    struct thread* t = thread_current();

    if (!is_valid_int(&args[1])) {
      process_exit(-2);
    }

    int fd = args[1];

    file_descriptor_t* table_entry = find_fd(t->pcb->file_descriptor_table, fd);

    if (table_entry == NULL) {
      //set the return value
      f->eax = false;
      //release the filesystem lock and return

      return;

    } else {

      f->eax = table_entry->is_dir;
      return;
    }
  }

  else if (args[0] == SYS_BLOCK_WRITES) {

    int fd = args[1];

    if (fd == 1) {

      f->eax = fs_device->read_cnt;
      return;
    } else if (fd == 0) {
      f->eax = fs_device->write_cnt;
      return;
    }

    return;
  }
}
