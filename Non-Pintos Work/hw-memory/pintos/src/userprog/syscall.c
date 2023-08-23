#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"

static void syscall_handler(struct intr_frame*);

static bool install_page(void* upage, void* kpage, bool writable);

static bool install_page(void* upage, void* kpage, bool writable) {
  struct thread* t = thread_current();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page(t->pagedir, upage) == NULL &&
          pagedir_set_page(t->pagedir, upage, kpage, writable));
}

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

void syscall_exit(int status) {
  printf("%s: exit(%d)\n", thread_current()->name, status);
  thread_exit();
}

/*
 * This does not check that the buffer consists of only mapped pages; it merely
 * checks the buffer exists entirely below PHYS_BASE.
 */
static void validate_buffer_in_user_region(const void* buffer, size_t length) {
  uintptr_t delta = PHYS_BASE - buffer;
  if (!is_user_vaddr(buffer) || length > delta)
    syscall_exit(-1);
}

/*
 * This does not check that the string consists of only mapped pages; it merely
 * checks the string exists entirely below PHYS_BASE.
 */
static void validate_string_in_user_region(const char* string) {
  uintptr_t delta = PHYS_BASE - (const void*)string;
  if (!is_user_vaddr(string) || strnlen(string, delta) == delta)
    syscall_exit(-1);
}

static int syscall_open(const char* filename) {
  struct thread* t = thread_current();
  if (t->open_file != NULL)
    return -1;

  t->open_file = filesys_open(filename);
  if (t->open_file == NULL)
    return -1;

  return 2;
}

static int syscall_write(int fd, void* buffer, unsigned size) {
  struct thread* t = thread_current();
  if (fd == STDOUT_FILENO) {
    putbuf(buffer, size);
    return size;
  } else if (fd != 2 || t->open_file == NULL)
    return -1;

  return (int)file_write(t->open_file, buffer, size);
}

static int syscall_read(int fd, void* buffer, unsigned size) {
  struct thread* t = thread_current();
  if (fd != 2 || t->open_file == NULL)
    return -1;

  return (int)file_read(t->open_file, buffer, size);
}

static void syscall_close(int fd) {
  struct thread* t = thread_current();
  if (fd == 2 && t->open_file != NULL) {
    file_close(t->open_file);
    t->open_file = NULL;
  }
}

static uint32_t syscall_sbrk(intptr_t increment, uint32_t stack){

  struct thread* t = thread_current();

  uint32_t old = t->brk;
  uint32_t new_page = (uint32_t) increment + old;

  if (increment == 0) {
    return old;
  }

  if ((uint32_t) pg_round_up((void *) new_page) >= stack || (uint32_t) pg_round_up((void *)new_page) >= (uint32_t) PHYS_BASE) {
    return (uint32_t) -1;
  }

  if (new_page < old) {
    
    void *old_start = pg_round_down((void*) old);
    void *new_start = pg_round_down((void*) (new_page - 1));

    while ((uint32_t) old_start > (uint32_t) new_start && (uint32_t) old_start >= t->heap_start) {
      void *p = pagedir_get_page(t->pagedir, old_start);
      pagedir_clear_page(t->pagedir, old_start);
      palloc_free_page(p);
      old_start = (void*) ((uint32_t) old_start - (uint32_t) PGSIZE);
    }
  } else if (new_page > (uint32_t) pg_round_up((void*) old)) {

    void *old_end = pg_round_up((void*) old);
    void *new_end = pg_round_up((void*) new_page);

    while ((uint32_t) old_end < (uint32_t) new_end) {

      void *p = palloc_get_page(PAL_ZERO | PAL_USER);
      if (p == NULL) {
        for (void* cur = pg_round_up((void*) old); cur <= old_end; cur += PGSIZE) {
          void* page = pagedir_get_page(t->pagedir, cur);
          pagedir_clear_page(t->pagedir, cur);
          palloc_free_page(page);
        }
        return (uint32_t)-1;
      }

      if (!install_page(old_end, p, true)) {
        palloc_free_page(p);
        return (uint32_t)-1;
      }

      old_end  = (void*) ((uint32_t) old_end + (uint32_t) PGSIZE);
    }
  } 

  t->brk = new_page;
  return old;

}


static void syscall_handler(struct intr_frame* f) {
  uint32_t* args = (uint32_t*)f->esp;
  struct thread* t = thread_current();
  t->in_syscall = true;

  validate_buffer_in_user_region(args, sizeof(uint32_t));
  switch (args[0]) {
    case SYS_EXIT:
      validate_buffer_in_user_region(&args[1], sizeof(uint32_t));
      syscall_exit((int)args[1]);
      break;

    case SYS_OPEN:
      validate_buffer_in_user_region(&args[1], sizeof(uint32_t));
      validate_string_in_user_region((char*)args[1]);
      f->eax = (uint32_t)syscall_open((char*)args[1]);
      break;

    case SYS_WRITE:
      validate_buffer_in_user_region(&args[1], 3 * sizeof(uint32_t));
      validate_buffer_in_user_region((void*)args[2], (unsigned)args[3]);
      f->eax = (uint32_t)syscall_write((int)args[1], (void*)args[2], (unsigned)args[3]);
      break;

    case SYS_READ:
      validate_buffer_in_user_region(&args[1], 3 * sizeof(uint32_t));
      validate_buffer_in_user_region((void*)args[2], (unsigned)args[3]);
      f->eax = (uint32_t)syscall_read((int)args[1], (void*)args[2], (unsigned)args[3]);
      break;

    case SYS_CLOSE:
      validate_buffer_in_user_region(&args[1], sizeof(uint32_t));
      syscall_close((int)args[1]);
      break;

    case SYS_SBRK:
      validate_buffer_in_user_region(&args[1], sizeof(uint32_t));
      f->eax = syscall_sbrk((intptr_t) args[1], (uint32_t) f->esp);
      break;

    default:
      printf("Unimplemented system call: %d\n", (int)args[0]);
      break;
  }

  t->in_syscall = false;
}
