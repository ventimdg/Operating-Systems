#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/process.h"

/* Partition that contains the file system. */
struct block* fs_device;

static void do_format(void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init(bool format) {
  fs_device = block_get_role(BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC("No file system device found, can't initialize file system.");

  list_init(&cache_entry_metadata_list);
  rw_lock_init(&cache_lock);
  lock_init(&cache_bitmap_lock);
  lock_init(&open_inode_lock);
  cache_bitmap = bitmap_create(64);

  inode_init();
  free_map_init();

  if (format)
    do_format();

  free_map_open();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done(void) {
  free_map_close();
  while (!list_empty(&cache_entry_metadata_list)) {
    struct list_elem* e;
    for (e = list_begin(&cache_entry_metadata_list); e != list_end(&cache_entry_metadata_list);) {
      cache_entry_metadata* entry = list_entry(e, cache_entry_metadata, cache_list_elem);
      if (!entry->removed && entry->dirty) {
        uint8_t* bounce = NULL;
        bounce = malloc(BLOCK_SECTOR_SIZE);
        ASSERT(bounce != NULL);
        memcpy(bounce, get_cache() + entry->cache_entry_location * BLOCK_SECTOR_SIZE,
               BLOCK_SECTOR_SIZE);
        block_write(fs_device, entry->sector_number, bounce);
      }
      e = list_next(e);
      list_remove(&entry->cache_list_elem);
      free(entry);
    }
  }
}
/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool filesys_create(const char* name, off_t initial_size, bool is_dir) {

  block_sector_t inode_sector = 0;

  char* new_name = name;

  struct dir* dir = resolve_path(new_name);

  if (dir == NULL) {
    return false;
  }

  char part[NAME_MAX + 1];

  int holder = get_next_part(part, &new_name);

  while (holder != 0) {
    holder = get_next_part(part, &new_name);
  }

  bool success =
      (dir != NULL && free_map_allocate(1, &inode_sector, NULL, 0) &&
       inode_create(inode_sector, initial_size) && dir_add(dir, part, inode_sector, is_dir));
  if (!success && inode_sector != 0)
    free_map_release(inode_sector, 1);
  dir_close(dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
void* filesys_open(const char* name, bool* is_dir) {

  if (strcmp(name, "/") == 0) {

    *is_dir = true;

    return dir_open_root();
  }

  struct inode* inode = NULL;

  char* new_name = name;

  struct dir* dir = resolve_path(new_name);

  if (dir == NULL) {
    return false;
  }

  char part[NAME_MAX + 1];

  int holder = get_next_part(part, &new_name);

  while (holder != 0) {
    holder = get_next_part(part, &new_name);
  }

  if (dir != NULL)
    dir_lookup(dir, part, &inode);
  dir_close(dir);

  if (inode == NULL) {
    return false;
  }

  *is_dir = inode->is_dir;

  if (*is_dir) {
    return dir_open(inode);
  } else {
    return file_open(inode);
  }
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool filesys_remove(const char* name) {

  struct inode* inode = NULL;

  char* new_name = name;

  struct dir* dir = resolve_path(new_name);

  if (dir == NULL) {
    return false;
  }

  char part[NAME_MAX + 1];

  int holder = get_next_part(part, &new_name);

  while (holder != 0) {
    holder = get_next_part(part, &new_name);
  }

  if (dir != NULL)
    dir_lookup(dir, part, &inode);

  if (inode == NULL) {
    dir_close(dir);
    inode_close(inode);
    return false;
  }

  bool success;

  if (!inode->is_dir) {

    success = dir != NULL && dir_remove(dir, part);

  } else {

    if (inode->ref_open != 0 || inode->ref_cwd != 0 || inode->is_root) {
      inode_close(inode);
      dir_close(dir);
      return false;
    }

    off_t ofs;

    struct dir_entry e;

    for (ofs = 2 * sizeof(e); inode_read_at(inode, &e, sizeof e, ofs) == sizeof e;
         ofs += sizeof e) {

      if (e.in_use) {
        inode_close(inode);
        dir_close(dir);
        return false;
      }
    }

    success = dir != NULL && dir_remove(dir, part);
  }

  inode_close(inode);
  dir_close(dir);
  return success;
}

/* Formats the file system. */
static void do_format(void) {
  printf("Formatting file system...");
  free_map_create();
  if (!dir_create(ROOT_DIR_SECTOR, 2, NULL))
    PANIC("root directory creation failed");
  free_map_close();
  printf("done.\n");
}

int get_next_part(char* part, char** srcp) {
  const char* src = *srcp;
  char* dst = part;

  /* Skip leading slashes.  If it's all slashes, we're done. */
  while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;

  /* Copy up to NAME_MAX character from SRC to DST.  Add null terminator. */
  while (*src != '/' && *src != '\0') {
    if (dst < part + NAME_MAX)
      *dst++ = *src;
    else
      return -1;
    src++;
  }
  *dst = '\0';

  /* Advance source pointer. */
  *srcp = src;
  return 1;
}

struct dir* resolve_path(char* path) {

  struct dir* dir;

  struct inode* next = NULL;

  char temp = path[0];

  if (strcmp(&temp, "/") == 0) {

    dir = dir_open_root();

  } else {

    struct thread* t = thread_current();

    dir = dir_open(inode_open(t->pcb->CWD));
  }

  char part[NAME_MAX + 1];

  int holder = get_next_part(part, &path);

  while (holder != 0) {

    if (holder == -1) {
      dir_close(dir);
      return NULL;
    }

    bool success = dir_lookup(dir, part, &next);

    holder = get_next_part(part, &path);

    if (!success && holder != 0) {
      return NULL;
    }

    if (holder == -1) {
      inode_close(next);
      dir_close(dir);
      return NULL;
    } else if (holder == 0) {

      inode_close(next);
      return dir;

    } else if (holder == 1) {

      if (!next->is_dir) {
        return NULL;
      }

      struct dir* temp = dir;

      dir = dir_open(next);

      dir_close(temp);
    }
  }
  return dir;
}
