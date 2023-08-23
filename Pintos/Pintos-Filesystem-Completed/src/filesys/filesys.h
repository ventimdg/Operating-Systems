#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "lib/kernel/list.h"
#include <bitmap.h>
#include "filesys/inode.h"
#include "threads/synch.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0 /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1 /* Root directory file inode sector. */

/* Block device that contains the file system. */
extern struct block* fs_device;

struct list cache_entry_metadata_list;
struct rw_lock cache_lock;
struct lock cache_bitmap_lock;
struct lock open_inode_lock;
struct bitmap* cache_bitmap;

void filesys_init(bool format);
void filesys_done(void);
bool filesys_create(const char* name, off_t initial_size, bool is_dir);
void* filesys_open(const char* name, bool* is_dir);
bool filesys_remove(const char* name);

int get_next_part(char* part, char** srcp);
struct dir* resolve_path(char* path);

#endif /* filesys/filesys.h */
