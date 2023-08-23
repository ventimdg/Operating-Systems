#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "lib/kernel/bitmap.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

static char cache[BLOCK_SECTOR_SIZE * 64];

char* get_cache(void) { return cache; }

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors(off_t size) { return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE); }

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector(struct inode* inode, off_t pos) {
  ASSERT(inode != NULL);

  struct inode_disk* data = (struct inode_disk*)malloc(sizeof(struct inode_disk));

  cache_read(fs_device, inode->sector, (void*)data, inode->sector);

  if (pos < data->length) {
    size_t num_sectors;
    if (pos == 0) {
      num_sectors = bytes_to_sectors(pos);
    } else {
      if (pos % BLOCK_SECTOR_SIZE == 0) {
        pos += 1;
      }
      num_sectors = bytes_to_sectors(pos) - 1;
    }
    if (num_sectors < 12) {

      block_sector_t answer = data->direct[num_sectors];
      free(data);

      return answer;

    } else if (num_sectors < 140) {

      size_t sector = num_sectors - 12;

      block_sector_t* buffer =
          (block_sector_t*)malloc(sizeof(block_sector_t) * (BLOCK_SECTOR_SIZE / 4));

      cache_read(fs_device, data->indirect, (void*)buffer, inode->sector);

      block_sector_t answer = buffer[sector];

      free(buffer);

      free(data);

      return answer;

    } else if (num_sectors < 16524) {

      size_t sector = num_sectors - 140;
      size_t index_1 = sector / 128;
      size_t index_2 = sector % 128;

      block_sector_t* buffer =
          (block_sector_t*)malloc(sizeof(block_sector_t) * (BLOCK_SECTOR_SIZE / 4));

      cache_read(fs_device, data->doubly_indirect, (void*)buffer, inode->sector);

      block_sector_t block2 = buffer[index_1];

      cache_read(fs_device, block2, (void*)buffer, inode->sector);

      block_sector_t answer = buffer[index_2];

      free(buffer);

      free(data);

      return answer;

    } else {
      free(data);
      return -1;
    }
  } else {
    free(data);
    return -1;
  }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void inode_init(void) { list_init(&open_inodes); }

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length) {
  struct inode_disk* disk_inode = NULL;
  bool success = false;

  ASSERT(length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc(1, sizeof *disk_inode);
  if (disk_inode != NULL) {
    size_t sectors = bytes_to_sectors(length);

    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;
    for (int i = 0; i < 12; i++) {
      disk_inode->direct[i] = 0;
    }
    disk_inode->indirect = 0;
    disk_inode->doubly_indirect = 0;

    block_sector_t* buffer =
        (block_sector_t*)malloc(sizeof(block_sector_t) * (BLOCK_SECTOR_SIZE / 4));

    if (free_map_allocate(sectors, &disk_inode->direct[0], disk_inode, sector)) {

      cache_write(fs_device, sector, disk_inode, sector);

      if (sectors > 0) {
        static char zeros[BLOCK_SECTOR_SIZE];
        size_t i;
        for (i = 0; i < sectors; i++) {
          if (i < 12) {

            cache_write(fs_device, disk_inode->direct[i], zeros, sector);

          } else if (i < 140) {
            size_t new_sector = i - 12;

            cache_read(fs_device, disk_inode->indirect, (void*)buffer, sector);

            cache_write(fs_device, buffer[new_sector], zeros, sector);

          } else if (i < 16524) {

            size_t new_sector = i - 140;
            size_t index_1 = new_sector / 128;
            size_t index_2 = new_sector % 128;

            cache_read(fs_device, disk_inode->doubly_indirect, (void*)buffer, sector);

            block_sector_t block2 = buffer[index_1];
            cache_read(fs_device, block2, (void*)buffer, sector);

            cache_write(fs_device, buffer[index_2], zeros, sector);
          }
        }
      }
      success = true;
    }
    free(buffer);
    free(disk_inode);
  }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode* inode_open(block_sector_t sector) {
  struct list_elem* e;
  struct inode* inode;

  /* Check whether this inode is already open. */
  lock_acquire(&open_inode_lock);
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e)) {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector) {
      lock_release(&open_inode_lock);
      inode_reopen(inode);
      return inode;
    }
  }
  lock_release(&open_inode_lock);

  /* Allocate memory. */
  inode = malloc(sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  lock_acquire(&open_inode_lock);
  list_push_front(&open_inodes, &inode->elem);
  lock_release(&open_inode_lock);

  rw_lock_init(&inode->inode_rwlock);
  rw_lock_init(&inode->resize_lock);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;

  if (sector == ROOT_DIR_SECTOR) {
    inode->is_root = true;
  } else {
    inode->is_root = false;
  }

  inode->ref_cwd = 0;
  inode->ref_open = 0;

  return inode;
}

/* Reopens and returns INODE. */
struct inode* inode_reopen(struct inode* inode) {
  if (inode != NULL) {
    rw_lock_acquire(&inode->inode_rwlock, false);
    inode->open_cnt++;
    rw_lock_release(&inode->inode_rwlock, false);
  }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber(struct inode* inode) { return inode->sector; }

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode* inode) {
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0) {
    /* Remove from inode list and release lock. */
    lock_acquire(&open_inode_lock);
    list_remove(&inode->elem);
    lock_release(&open_inode_lock);

    /* Deallocate blocks if removed. */
    if (inode->removed) {
      free_map_release(inode->sector, 1);

      bool single = false;
      bool duble = false;

      block_sector_t* double_indirect = (block_sector_t*)malloc(sizeof(block_sector_t) * 128);
      int* removing_double = (int*)malloc(sizeof(int) * 128);
      block_sector_t* buffer = (block_sector_t*)malloc(sizeof(block_sector_t) * 128);

      for (int i = 0; i < 128; i++) {
        removing_double[i] = 0;
      }

      struct inode_disk* data = (struct inode_disk*)malloc(sizeof(struct inode_disk));
      cache_read(fs_device, inode->sector, (void*)data, inode->sector);

      for (unsigned i = 0; i < bytes_to_sectors(data->length); i++) {
        if (i < 12) {
          free_map_release(data->direct[i], 1);
        } else if (i < 140) {
          single = true;
          size_t new_sector = i - 12;
          cache_read(fs_device, data->indirect, (void*)buffer, inode->sector);
          free_map_release(buffer[new_sector], 1);

        } else if (i < 16524) {
          duble = true;
          size_t new_sector = i - 140;
          size_t index_1 = new_sector / 128;
          size_t index_2 = new_sector % 128;
          cache_read(fs_device, data->doubly_indirect, (void*)buffer, inode->sector);
          block_sector_t block2 = buffer[index_1];
          cache_read(fs_device, block2, (void*)buffer, inode->sector);
          free_map_release(buffer[index_2], 1);
          removing_double[index_1] = 1;
          double_indirect[index_1] = block2;
        }
      }

      if (single) {
        free_map_release(data->indirect, 1);
      }

      if (duble) {
        free_map_release(data->doubly_indirect, 1);
        for (int i = 0; i < 128; i++) {
          if (removing_double[i] == 1) {
            free_map_release(double_indirect[i], 1);
          } else {
            break;
          }
        }
      }

      struct list_elem* e;
      for (e = list_begin(&cache_entry_metadata_list); e != list_end(&cache_entry_metadata_list);
           e = list_next(e)) {
        cache_entry_metadata* entry = list_entry(e, cache_entry_metadata, cache_list_elem);
        if (inode->sector == entry->inumber) {
          entry->removed = true;
        }
      }

      free(double_indirect);
      free(removing_double);
      free(buffer);
      free(data);
    }
    free(inode);
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove(struct inode* inode) {
  rw_lock_acquire(&inode->inode_rwlock, false);
  ASSERT(inode != NULL);
  inode->removed = true;
  rw_lock_release(&inode->inode_rwlock, false);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode* inode, void* buffer_, off_t size, off_t offset) {
  uint8_t* buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t* bounce = NULL;
  rw_lock_acquire(&inode->resize_lock, true);

  while (size > 0) {
    /* Disk sector to read, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;
    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Read full sector directly into caller's buffer. */

      cache_read(fs_device, sector_idx, buffer + bytes_read, inode->sector);
    } else {
      /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
      if (bounce == NULL) {
        bounce = malloc(BLOCK_SECTOR_SIZE);
        if (bounce == NULL)
          break;
      }

      cache_read(fs_device, sector_idx, bounce, inode->sector);
      memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }

  rw_lock_release(&inode->resize_lock, true);
  free(bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t inode_write_at(struct inode* inode, const void* buffer_, off_t size, off_t offset) {
  rw_lock_acquire(&inode->resize_lock, false);

  const uint8_t* buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t* bounce = NULL;
  off_t new_length = size + offset;
  bool here = false;

  struct inode_disk* data = (struct inode_disk*)malloc(sizeof(struct inode_disk));
  cache_read(fs_device, inode->sector, (void*)data, inode->sector);

  if (new_length > data->length) {
    here = true;
    off_t bytes_left_last_sector = BLOCK_SECTOR_SIZE - (data->length % BLOCK_SECTOR_SIZE);
    if (bytes_left_last_sector >= new_length - data->length && bytes_left_last_sector != 512) {

      data->length = new_length;
      cache_write(fs_device, inode->sector, (void*)data, inode->sector);

    } else {

      inode_resize(inode, new_length);
    }
  }
  if (!here) {
    rw_lock_release(&inode->resize_lock, false);
  }

  if (inode->deny_write_cnt) {
    if (here) {
      rw_lock_release(&inode->resize_lock, false);
    }
    return 0;
  }

  while (size > 0) {
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Write full sector directly to disk. */

      cache_write(fs_device, sector_idx, buffer + bytes_written, inode->sector);
    } else {
      /* We need a bounce buffer. */
      if (bounce == NULL) {
        bounce = malloc(BLOCK_SECTOR_SIZE);
        if (bounce == NULL)
          break;
      }

      /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
      if (sector_ofs > 0 || chunk_size < sector_left)

        cache_read(fs_device, sector_idx, bounce, inode->sector);
      else
        memset(bounce, 0, BLOCK_SECTOR_SIZE);
      memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);

      cache_write(fs_device, sector_idx, bounce, inode->sector);
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }
  if (here) {
    rw_lock_release(&inode->resize_lock, false);
  }
  free(bounce);
  free(data);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write(struct inode* inode) {
  rw_lock_acquire(&inode->inode_rwlock, false);
  inode->deny_write_cnt++;
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  rw_lock_release(&inode->inode_rwlock, false);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode* inode) {
  rw_lock_acquire(&inode->inode_rwlock, false);
  ASSERT(inode->deny_write_cnt > 0);
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  rw_lock_release(&inode->inode_rwlock, false);
}

/* Returns the length, in bytes, of INODE's data-> */
off_t inode_length(struct inode* inode) {

  struct inode_disk* data = (struct inode_disk*)malloc(sizeof(struct inode_disk));
  cache_read(fs_device, inode->sector, (void*)data, inode->sector);

  off_t answer = data->length;

  free(data);

  return answer;
}

void cache_read(struct block* block, block_sector_t sector_number, void* store_read_data,
                block_sector_t inumber) {

  rw_lock_acquire(&cache_lock, false);
  cache_entry_metadata* found = find(block, sector_number);
  rw_lock_release(&cache_lock, false);

  if (found == NULL) {

    lock_acquire(&cache_bitmap_lock);
    size_t position = bitmap_scan_and_flip(cache_bitmap, 0, 1, false);
    lock_release(&cache_bitmap_lock);

    if (position != BITMAP_ERROR) {

      ASSERT((int)position >= 0);
      ASSERT((int)position <= 64);
      cache_entry_metadata* new_entry = (cache_entry_metadata*)malloc(sizeof(cache_entry_metadata));
      rw_lock_init(&new_entry->rwlock);
      lock_init(&new_entry->ref_cnt_lock);
      new_entry->cache_entry_location = (int)position;
      new_entry->dirty = false;
      new_entry->inumber = inumber;
      new_entry->sector_number = sector_number;
      new_entry->removed = false;
      new_entry->ref_count_eviction = 1;
      new_entry->block = block;
      rw_lock_acquire(&new_entry->rwlock, false);

      rw_lock_acquire(&cache_lock, false);
      list_push_back(&cache_entry_metadata_list, &new_entry->cache_list_elem);
      rw_lock_release(&cache_lock, false);

      block_read(block, sector_number, store_read_data);
      memcpy(cache + (int)position * BLOCK_SECTOR_SIZE, store_read_data, BLOCK_SECTOR_SIZE);

      rw_lock_release(&new_entry->rwlock, false);

      lock_acquire(&new_entry->ref_cnt_lock);
      new_entry->ref_count_eviction--;
      lock_release(&new_entry->ref_cnt_lock);

      return;
    } else {
      rw_lock_acquire(&cache_lock, false);
      cache_entry_metadata* new_entry = cache_evict();

      rw_lock_acquire(&new_entry->rwlock, false);
      new_entry->dirty = false;
      new_entry->inumber = inumber;
      new_entry->sector_number = sector_number;
      new_entry->removed = false;
      new_entry->ref_count_eviction = 1;
      new_entry->block = block;

      rw_lock_acquire(&cache_lock, false);
      list_push_back(&cache_entry_metadata_list, &new_entry->cache_list_elem);
      rw_lock_release(&cache_lock, false);

      block_read(block, sector_number, store_read_data);
      memcpy(cache + new_entry->cache_entry_location * BLOCK_SECTOR_SIZE, store_read_data,
             BLOCK_SECTOR_SIZE);

      rw_lock_release(&new_entry->rwlock, false);

      lock_acquire(&new_entry->ref_cnt_lock);
      new_entry->ref_count_eviction--;
      lock_release(&new_entry->ref_cnt_lock);

      return;
    }
  } else {

    rw_lock_acquire(&found->rwlock, true);

    memcpy(store_read_data, cache + found->cache_entry_location * BLOCK_SECTOR_SIZE,
           BLOCK_SECTOR_SIZE);

    rw_lock_release(&found->rwlock, true);

    lock_acquire(&found->ref_cnt_lock);
    found->ref_count_eviction--;
    lock_release(&found->ref_cnt_lock);

    return;
  }
}

void cache_write(struct block* block, block_sector_t sector_number, void* data_to_write,
                 block_sector_t inumber) {

  rw_lock_acquire(&cache_lock, false);
  cache_entry_metadata* found = find(block, sector_number);
  rw_lock_release(&cache_lock, false);

  if (found == NULL) {

    lock_acquire(&cache_bitmap_lock);
    size_t position = bitmap_scan_and_flip(cache_bitmap, 0, 1, false);
    lock_release(&cache_bitmap_lock);

    if (position != BITMAP_ERROR) {

      ASSERT((int)position >= 0);
      ASSERT((int)position <= 64);

      cache_entry_metadata* new_entry = (cache_entry_metadata*)malloc(sizeof(cache_entry_metadata));

      rw_lock_init(&new_entry->rwlock);
      lock_init(&new_entry->ref_cnt_lock);
      new_entry->cache_entry_location = (int)position;
      new_entry->dirty = true;
      new_entry->inumber = inumber;
      new_entry->sector_number = sector_number;
      new_entry->removed = false;
      new_entry->ref_count_eviction = 1;
      new_entry->block = block;

      rw_lock_acquire(&new_entry->rwlock, false);

      rw_lock_acquire(&cache_lock, false);
      list_push_back(&cache_entry_metadata_list, &new_entry->cache_list_elem);
      rw_lock_release(&cache_lock, false);

      memcpy(cache + (int)position * BLOCK_SECTOR_SIZE, data_to_write, BLOCK_SECTOR_SIZE);

      rw_lock_release(&new_entry->rwlock, false);

      lock_acquire(&new_entry->ref_cnt_lock);
      new_entry->ref_count_eviction--;
      lock_release(&new_entry->ref_cnt_lock);

      return;

    } else {

      rw_lock_acquire(&cache_lock, false);
      cache_entry_metadata* new_entry = cache_evict();

      rw_lock_acquire(&new_entry->rwlock, false);
      new_entry->dirty = true;
      new_entry->inumber = inumber;
      new_entry->sector_number = sector_number;
      new_entry->removed = false;
      new_entry->ref_count_eviction = 1;
      new_entry->block = block;

      rw_lock_acquire(&cache_lock, false);
      list_push_back(&cache_entry_metadata_list, &new_entry->cache_list_elem);
      rw_lock_release(&cache_lock, false);

      memcpy(cache + new_entry->cache_entry_location * BLOCK_SECTOR_SIZE, data_to_write,
             BLOCK_SECTOR_SIZE);

      rw_lock_release(&new_entry->rwlock, false);

      lock_acquire(&new_entry->ref_cnt_lock);
      new_entry->ref_count_eviction--;
      lock_release(&new_entry->ref_cnt_lock);
      return;
    }
  } else {

    rw_lock_acquire(&found->rwlock, false);

    memcpy(cache + found->cache_entry_location * BLOCK_SECTOR_SIZE, data_to_write,
           BLOCK_SECTOR_SIZE);

    found->dirty = true;

    rw_lock_release(&found->rwlock, false);

    lock_acquire(&found->ref_cnt_lock);
    found->ref_count_eviction--;
    lock_release(&found->ref_cnt_lock);

    return;
  }
}

cache_entry_metadata* cache_evict(void) {
  cache_entry_metadata* found = NULL;
  struct list_elem* e;
  for (e = list_begin(&cache_entry_metadata_list); e != list_end(&cache_entry_metadata_list);
       e = list_next(e)) {
    cache_entry_metadata* entry = list_entry(e, cache_entry_metadata, cache_list_elem);
    if (entry->ref_count_eviction == 0) {
      found = entry;
      list_remove(e);
      break;
    }
  }

  rw_lock_release(&cache_lock, false);

  ASSERT(found != NULL);

  if (!found->removed && found->dirty) {
    uint8_t* bounce = NULL;
    bounce = malloc(BLOCK_SECTOR_SIZE);
    ASSERT(bounce != NULL);
    memcpy(bounce, cache + found->cache_entry_location * BLOCK_SECTOR_SIZE, BLOCK_SECTOR_SIZE);
    block_write(fs_device, found->sector_number, bounce);
    free(bounce);
  }

  return found;
}

cache_entry_metadata* find(struct block* block, block_sector_t sector_number) {
  struct list_elem* e;
  for (e = list_begin(&cache_entry_metadata_list); e != list_end(&cache_entry_metadata_list);
       e = list_next(e)) {
    cache_entry_metadata* entry = list_entry(e, cache_entry_metadata, cache_list_elem);
    if (entry->sector_number == sector_number && entry->block == block) {
      list_remove(e);
      list_push_back(&cache_entry_metadata_list, e);

      rw_lock_release(&cache_lock, false);

      lock_acquire(&entry->ref_cnt_lock);
      entry->ref_count_eviction++;
      lock_release(&entry->ref_cnt_lock);

      rw_lock_acquire(&cache_lock, false);

      return entry;
    }
  }
  return NULL;
}

bool inode_resize(struct inode* id, off_t size) {

  static char zeros[BLOCK_SECTOR_SIZE];
  int num_allocated = 0;
  block_sector_t* newly_allocated =
      (block_sector_t*)malloc(sizeof(block_sector_t) * DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE));

  int num_allocated_double = 0;
  block_sector_t** newly_allocated_pointer =
      (block_sector_t**)malloc(sizeof(block_sector_t*) * 128);
  block_sector_t* newly_allocated_pointer_parents =
      (block_sector_t*)malloc(sizeof(block_sector_t) * 128);

  struct inode_disk* data = (struct inode_disk*)malloc(sizeof(struct inode_disk));
  cache_read(fs_device, id->sector, (void*)data, id->sector);

  for (int i = 0; i < 12; i++) {
    if (size <= BLOCK_SECTOR_SIZE * i && data->direct[i] != 0) {
      free_map_release(data->direct[i], 1);
      data->direct[i] = 0;
    } else if (size > BLOCK_SECTOR_SIZE * i && data->direct[i] == 0) {
      bool success = free_map_allocate(1, &data->direct[i], data, 0);
      if (!success) {
        inode_resize(id, data->length);
        free(newly_allocated);
        free(newly_allocated_pointer);
        free(newly_allocated_pointer_parents);
        free(data);
        return false;
      } else {
        newly_allocated[num_allocated] = data->direct[i];
        num_allocated++;
      }
    }
  }

  if (data->indirect == 0 && size <= 12 * BLOCK_SECTOR_SIZE) {
    for (int i = 0; i < num_allocated; i++) {
      cache_write(fs_device, newly_allocated[i], zeros, id->sector);
    }
    data->length = size;
    free(newly_allocated);
    free(newly_allocated_pointer);
    free(newly_allocated_pointer_parents);
    cache_write(fs_device, id->sector, (void*)data, id->sector);
    free(data);
    return true;
  }

  block_sector_t* indirect_block = (block_sector_t*)malloc(sizeof(block_sector_t) * 128);

  memset(indirect_block, 0, BLOCK_SECTOR_SIZE);

  if (data->indirect == 0) {
    bool success = free_map_allocate(1, &data->indirect, data, 0);
    if (!success) {
      inode_resize(id, data->length);
      free(newly_allocated);
      free(newly_allocated_pointer);
      free(newly_allocated_pointer_parents);
      free(indirect_block);
      free(data);
      return false;
    }
  } else {
    cache_read(fs_device, data->indirect, (void*)indirect_block, id->sector);
  }

  for (int i = 0; i < 128; i++) {
    if (size <= (12 + i) * BLOCK_SECTOR_SIZE && indirect_block[i] != 0) {
      free_map_release(indirect_block[i], 1);
      indirect_block[i] = 0;
    } else if (size > (12 + i) * BLOCK_SECTOR_SIZE && indirect_block[i] == 0) {
      bool success = free_map_allocate(1, &indirect_block[i], data, 0);
      if (!success) {
        inode_resize(id, data->length);
        free(newly_allocated);
        free(newly_allocated_pointer);
        free(newly_allocated_pointer_parents);
        free(indirect_block);
        free(data);
        return false;
      } else {
        newly_allocated[num_allocated] = indirect_block[i];
        num_allocated++;
      }
    }
  }

  if (size <= 12 * BLOCK_SECTOR_SIZE) {
    free_map_release(data->indirect, 1);
    data->indirect = 0;
  }

  if (data->doubly_indirect == 0 && size <= 140 * BLOCK_SECTOR_SIZE) {
    for (int i = 0; i < num_allocated; i++) {
      cache_write(fs_device, newly_allocated[i], zeros, id->sector);
    }
    cache_write(fs_device, data->indirect, indirect_block, id->sector);
    data->length = size;
    free(newly_allocated);
    free(newly_allocated_pointer);
    free(newly_allocated_pointer_parents);
    free(indirect_block);
    cache_write(fs_device, id->sector, (void*)data, id->sector);
    free(data);
    return true;
  }

  block_sector_t* doubly_indirect_block = (block_sector_t*)malloc(sizeof(block_sector_t) * 128);
  block_sector_t* doubly_indirect_block_pointers =
      (block_sector_t*)malloc(sizeof(block_sector_t) * 128);

  memset(doubly_indirect_block, 0, BLOCK_SECTOR_SIZE);

  if (data->doubly_indirect == 0) {
    bool success = free_map_allocate(1, &data->doubly_indirect, data, 0);
    if (!success) {
      inode_resize(id, data->length);
      free(newly_allocated);
      free(newly_allocated_pointer);
      free(newly_allocated_pointer_parents);
      free(indirect_block);
      free(doubly_indirect_block);
      free(doubly_indirect_block_pointers);
      free(data);
      return false;
    }
  } else {
    cache_read(fs_device, data->doubly_indirect, (void*)doubly_indirect_block, id->sector);
  }

  for (int i = 0; i < 128; i++) {
    if (size <= (140 + i * 128) * BLOCK_SECTOR_SIZE && doubly_indirect_block[i] != 0) {

      cache_read(fs_device, doubly_indirect_block[i], (void*)doubly_indirect_block_pointers,
                 id->sector);

      for (int k = 0; k < 128; k++) {
        if (doubly_indirect_block_pointers[k] != 0) {
          free_map_release(doubly_indirect_block_pointers[k], 1);
        } else {
          break;
        }
      }

      free_map_release(doubly_indirect_block[i], 1);
      doubly_indirect_block[i] = 0;

    } else if (size > (140 + i * 128) * BLOCK_SECTOR_SIZE && doubly_indirect_block[i] == 0) {
      bool success = free_map_allocate(1, &doubly_indirect_block[i], data, 0);
      if (!success) {
        inode_resize(id, data->length);
        free(newly_allocated);
        free(newly_allocated_pointer);
        free(newly_allocated_pointer_parents);
        free(indirect_block);
        free(doubly_indirect_block);
        free(doubly_indirect_block_pointers);
        free(data);
        return false;
      }
      block_sector_t* new_double = (block_sector_t*)malloc(sizeof(block_sector_t) * 128);
      memset(new_double, 0, BLOCK_SECTOR_SIZE);

      for (int j = 0; j < 128; j++) {
        if (size > (140 + i * 128 + j) * BLOCK_SECTOR_SIZE) {
          bool success = free_map_allocate(1, &new_double[j], data, 0);
          if (!success) {
            inode_resize(id, data->length);
            free(newly_allocated);
            free(newly_allocated_pointer);
            free(newly_allocated_pointer_parents);
            free(indirect_block);
            free(doubly_indirect_block);
            free(doubly_indirect_block_pointers);
            free(new_double);
            free(data);
            return false;
          } else {
            newly_allocated[num_allocated] = new_double[j];
            num_allocated++;
          }
        }
      }
      newly_allocated_pointer[num_allocated_double] = new_double;
      newly_allocated_pointer_parents[num_allocated_double] = doubly_indirect_block[i];
      num_allocated_double++;

    } else if (size > (140 + i * 128) * BLOCK_SECTOR_SIZE && doubly_indirect_block[i] != 0) {
      cache_read(fs_device, doubly_indirect_block[i], (void*)doubly_indirect_block_pointers,
                 id->sector);
      for (int j = 0; j < 128; j++) {
        if (size > (140 + i * 128 + j) * BLOCK_SECTOR_SIZE &&
            doubly_indirect_block_pointers[j] == 0) {
          bool success = free_map_allocate(1, &doubly_indirect_block_pointers[j], data, 0);
          if (!success) {
            inode_resize(id, data->length);
            free(newly_allocated);
            free(newly_allocated_pointer);
            free(newly_allocated_pointer_parents);
            free(indirect_block);
            free(doubly_indirect_block);
            free(doubly_indirect_block_pointers);
            free(data);
            return false;
          } else {
            newly_allocated[num_allocated] = doubly_indirect_block_pointers[j];
            num_allocated++;
          }
        } else if (size <= (140 + i * 128 + j) * BLOCK_SECTOR_SIZE &&
                   doubly_indirect_block_pointers[j] != 0) {
          free_map_release(doubly_indirect_block_pointers[j], 1);
          doubly_indirect_block_pointers[j] = 0;
        }
      }
      cache_write(fs_device, doubly_indirect_block[i], (void*)doubly_indirect_block_pointers,
                  id->sector);
    }
  }

  if (size <= 140 * BLOCK_SECTOR_SIZE) {
    free_map_release(data->doubly_indirect, 1);
    data->doubly_indirect = 0;
  }

  if (data->doubly_indirect == 0) {
    for (int i = 0; i < num_allocated; i++) {
      cache_write(fs_device, newly_allocated[i], zeros, id->sector);
    }
    cache_write(fs_device, data->indirect, indirect_block, id->sector);
    data->length = size;
    free(newly_allocated);
    free(newly_allocated_pointer);
    free(newly_allocated_pointer_parents);
    free(indirect_block);
    free(doubly_indirect_block);
    free(doubly_indirect_block_pointers);
    cache_write(fs_device, id->sector, (void*)data, id->sector);
    free(data);
    return true;
  } else {
    for (int i = 0; i < num_allocated; i++) {
      cache_write(fs_device, newly_allocated[i], zeros, id->sector);
    }
    cache_write(fs_device, data->indirect, indirect_block, id->sector);
    cache_write(fs_device, data->doubly_indirect, doubly_indirect_block, id->sector);
    for (int i = 0; i < num_allocated_double; i++) {
      cache_write(fs_device, newly_allocated_pointer_parents[i], (void*)newly_allocated_pointer[i],
                  id->sector);
      free(newly_allocated_pointer[i]);
    }
    data->length = size;
    free(newly_allocated);
    free(newly_allocated_pointer);
    free(newly_allocated_pointer_parents);
    free(indirect_block);
    free(doubly_indirect_block);
    free(doubly_indirect_block_pointers);
    cache_write(fs_device, id->sector, (void*)data, id->sector);
    free(data);
    return true;
  }
}
