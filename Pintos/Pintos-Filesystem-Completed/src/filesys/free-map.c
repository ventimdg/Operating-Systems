#include "filesys/free-map.h"
#include <bitmap.h>
#include <debug.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"

static struct file* free_map_file; /* Free map file. */
static struct bitmap* free_map;    /* Free map, one bit per sector. */
static struct lock free_map_lock;

/* Initializes the free map. */
void free_map_init(void) {
  free_map = bitmap_create(block_size(fs_device));
  lock_init(&free_map_lock);
  if (free_map == NULL)
    PANIC("bitmap creation failed--file system device is too large");
  bitmap_mark(free_map, FREE_MAP_SECTOR);
  bitmap_mark(free_map, ROOT_DIR_SECTOR);
}

/* Allocates CNT consecutive sectors from the free map and stores
   the first into *SECTORP.
   Returns true if successful, false if not enough consecutive
   sectors were available or if the free_map file could not be
   written. */
bool free_map_allocate(size_t cnt, block_sector_t* sectorp, struct inode_disk* disk_inode,
                       block_sector_t inumber) {

  lock_acquire(&free_map_lock);

  if (cnt == 1 || disk_inode == NULL) {
    block_sector_t sector = bitmap_scan_and_flip(free_map, 0, cnt, false);
    if (sector != BITMAP_ERROR && free_map_file != NULL && !bitmap_write(free_map, free_map_file)) {
      bitmap_set_multiple(free_map, sector, cnt, false);
      sector = BITMAP_ERROR;
    }
    if (sector != BITMAP_ERROR)
      *sectorp = sector;
    lock_release(&free_map_lock);
    return sector != BITMAP_ERROR;
  } else {
    block_sector_t indirect_block[BLOCK_SECTOR_SIZE / 4];
    block_sector_t double_indirect_block[BLOCK_SECTOR_SIZE / 4];

    int double_indirect = 0;
    int indirect = 0;

    if (cnt > 12) {
      indirect = 1;
      memset((void*)indirect_block, 0, BLOCK_SECTOR_SIZE);
    }

    if (cnt > 140) {
      int calc = cnt - 140;
      double_indirect = (calc - 1) / 128 + 2;
      memset((void*)double_indirect_block, 0, BLOCK_SECTOR_SIZE);
    }

    int true_cnt = cnt + double_indirect + indirect;
    bool dont_continue = false;

    if (bitmap_count(free_map, 0, block_size(fs_device), false) >= (unsigned)true_cnt) {
      block_sector_t start_sector = bitmap_scan_and_flip(free_map, 0, true_cnt, false);
      if (start_sector != BITMAP_ERROR && free_map_file != NULL &&
          !bitmap_write(free_map, free_map_file)) {
        bitmap_set_multiple(free_map, start_sector, cnt, false);
        start_sector = BITMAP_ERROR;
        dont_continue = true;
      }
      if (start_sector != BITMAP_ERROR) {
        for (int i = 0; i < true_cnt; i++) {
          if (i < 12) {
            disk_inode->direct[i] = start_sector + i;
          } else if (i == 12) {
            disk_inode->indirect = start_sector + i;
          } else if (i < 141) {
            int new_sec = i - 13;
            indirect_block[new_sec] = start_sector + i;
            cache_write(fs_device, disk_inode->indirect, (void*)indirect_block, inumber);
          } else if (i == 142) {
            disk_inode->doubly_indirect = start_sector + i;
          } else {
            if ((i - 143) % 129 == 0) {
              int second_block = (i - 143) / 129;
              block_sector_t temp[BLOCK_SECTOR_SIZE / 4];
              cache_read(fs_device, disk_inode->doubly_indirect, (void*)temp, inumber);
              temp[second_block] = start_sector + i;
              cache_write(fs_device, disk_inode->doubly_indirect, (void*)temp, inumber);
            } else {
              int second_block = (i - 143) / 129;
              int second_block_index = (i - 143) % 129 - 1;
              block_sector_t temp[BLOCK_SECTOR_SIZE / 4];
              cache_read(fs_device, disk_inode->doubly_indirect, (void*)temp, inumber);
              block_sector_t next = temp[second_block];
              cache_read(fs_device, next, (void*)temp, inumber);
              temp[second_block_index] = start_sector + i;
              cache_write(fs_device, next, (void*)temp, inumber);
            }
          }
        }
        lock_release(&free_map_lock);
        return start_sector != BITMAP_ERROR;
      } else if (!dont_continue) {
        block_sector_t allocated[true_cnt];
        for (int i = 0; i < true_cnt; i++) {
          block_sector_t start_sector = bitmap_scan_and_flip(free_map, 0, 1, false);
          allocated[i] = start_sector;
          if (i < 12) {
            disk_inode->direct[i] = start_sector;
          } else if (i == 12) {
            disk_inode->indirect = start_sector;
          } else if (i < 141) {
            int new_sec = i - 13;
            indirect_block[new_sec] = start_sector;
            cache_write(fs_device, disk_inode->indirect, (void*)indirect_block, inumber);
          } else if (i == 142) {
            disk_inode->doubly_indirect = start_sector;
          } else {
            if ((i - 143) % 129 == 0) {
              int second_block = (i - 143) / 129;
              block_sector_t temp[BLOCK_SECTOR_SIZE / 4];
              cache_read(fs_device, disk_inode->doubly_indirect, (void*)temp, inumber);
              temp[second_block] = start_sector;
              cache_write(fs_device, disk_inode->doubly_indirect, (void*)temp, inumber);
            } else {
              int second_block = (i - 143) / 129;
              int second_block_index = (i - 143) % 129 - 1;
              block_sector_t temp[BLOCK_SECTOR_SIZE / 4];
              cache_read(fs_device, disk_inode->doubly_indirect, (void*)temp, inumber);
              block_sector_t next = temp[second_block];
              cache_read(fs_device, next, (void*)temp, inumber);
              temp[second_block_index] = start_sector;
              cache_write(fs_device, next, (void*)temp, inumber);
            }
          }
        }
        if (free_map_file != NULL && !bitmap_write(free_map, free_map_file)) {
          for (int i = 0; i < true_cnt; i++) {
            bitmap_set_multiple(free_map, allocated[i], 1, false);
          }
          lock_release(&free_map_lock);
          return false;
        } else {
          lock_release(&free_map_lock);
          return true;
        }
      }
    } else {
      lock_release(&free_map_lock);
      return false;
    }
  }
  return false;
}

/* Makes CNT sectors starting at SECTOR available for use. */
void free_map_release(block_sector_t sector, size_t cnt) {
  lock_acquire(&free_map_lock);
  ASSERT(bitmap_all(free_map, sector, cnt));
  bitmap_set_multiple(free_map, sector, cnt, false);
  bitmap_write(free_map, free_map_file);
  lock_release(&free_map_lock);
}

/* Opens the free map file and reads it from disk. */
void free_map_open(void) {
  free_map_file = file_open(inode_open(FREE_MAP_SECTOR));
  if (free_map_file == NULL)
    PANIC("can't open free map");
  if (!bitmap_read(free_map, free_map_file))
    PANIC("can't read free map");
}

/* Writes the free map to disk and closes the free map file. */
void free_map_close(void) { file_close(free_map_file); }

/* Creates a new free map file on disk and writes the free map to
   it. */
void free_map_create(void) {
  /* Create inode. */
  if (!inode_create(FREE_MAP_SECTOR, bitmap_file_size(free_map)))
    PANIC("free map creation failed");

  /* Write bitmap to file. */
  free_map_file = file_open(inode_open(FREE_MAP_SECTOR));
  if (free_map_file == NULL)
    PANIC("can't open free map");
  if (!bitmap_write(free_map, free_map_file))
    PANIC("can't write free map");
}
