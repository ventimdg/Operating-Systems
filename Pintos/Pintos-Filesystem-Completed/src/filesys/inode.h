#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"
#include "lib/kernel/list.h"

struct bitmap;

typedef struct {

  struct rw_lock rwlock;    // Used to ensure that readers and writers do not clash
  int cache_entry_location; // Used to index into the "long cache string". The data of that cache line is stored in the cache from index cache_entry_location to index cache_entry_location + BLOCK_SECTOR_SIZE - 1
  struct list_elem
      cache_list_elem; // Used to store each cache entry in a list so we can locate cache blocks and implement LRU replacement
  bool dirty;          // Set to 0 when clean and 1 when dirty. Check on eviction.
  block_sector_t
      inumber; // inumber of the in-memory inode that represents the file to which this sector belongs
  block_sector_t sector_number; // Sector number of the cached sector
  bool
      removed; // Whether or not the inode has been removed. If true, tells OS to not write back to disk if dirty.
  int ref_count_eviction; // Helps for the approximation of LRU
  struct lock ref_cnt_lock;
  struct block* block;

} cache_entry_metadata;

/* In-memory inode. */
struct inode {
  struct list_elem elem; /* Element in inode list. */
  block_sector_t sector; /* Sector number of disk location. */
  int open_cnt;          /* Number of openers. */
  bool removed;          /* True if deleted, false otherwise. */
  int deny_write_cnt;    /* 0: writes ok, >0: deny writes. */
  struct rw_lock inode_rwlock;
  struct rw_lock resize_lock;
  bool is_dir;
  int ref_open;
  int ref_cwd;
  bool is_root;
};

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk {

  block_sector_t direct[12];      /* Direct pointers */
  block_sector_t indirect;        /* Indirect pointer */
  block_sector_t doubly_indirect; /* Doubly Indirect pointer */
  off_t length;                   /* File size in bytes. */
  unsigned magic;                 /* Magic number. */
  uint32_t unused[112];           /* Not used. Ensure this inode fits perfectly in a sector  */
};

void inode_init(void);
bool inode_create(block_sector_t, off_t);
struct inode* inode_open(block_sector_t);
struct inode* inode_reopen(struct inode*);
block_sector_t inode_get_inumber(struct inode*);
void inode_close(struct inode*);
void inode_remove(struct inode*);
off_t inode_read_at(struct inode*, void*, off_t size, off_t offset);
off_t inode_write_at(struct inode*, const void*, off_t size, off_t offset);
void inode_deny_write(struct inode*);
void inode_allow_write(struct inode*);
off_t inode_length(struct inode*);

void cache_read(struct block* block, block_sector_t sector_number, void* store_read_data,
                block_sector_t inumber);

void cache_write(struct block* block, block_sector_t sector_number, void* data_to_write,
                 block_sector_t inumber);

cache_entry_metadata* cache_evict(void);

cache_entry_metadata* find(struct block* block, block_sector_t sector_number);

char* get_cache(void);

bool inode_resize(struct inode* id, off_t size);

#endif /* filesys/inode.h */
