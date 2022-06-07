#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define DIRECT_BLOCK_ENTRIES 123
#define INDIRECT_BLOCK_ENTRIES 128

enum direct_t{
NORMAL_DIRECT=0,
INDIRECT=1,
DOUBLE_INDIRECT=2,
OUT_LIMIT=3
};

struct sector_location{
 enum direct_t directness;
  off_t index1;
  off_t index2;
};

struct inode_indirect_block{
  block_sector_t map_table[INDIRECT_BLOCK_ENTRIES];
};

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t direct_map_table[DIRECT_BLOCK_ENTRIES];
    block_sector_t indirect_block_sec;
    block_sector_t double_indirect_block_sec;
    int is_dir;
  };

static bool get_disk_inode(const struct inode *inode, struct inode_disk *inode_disk);
static void locate_byte(off_t pos, struct sector_location *sec_loc);
static bool register_sector(struct inode_disk *inode_disk, block_sector_t new_sector, struct sector_location sec_loc);
static void free_inode_sectors(struct inode_disk *inode_disk);
bool inode_update_file_length(struct inode_disk *inode_disk, off_t start_pos, off_t end_pos);
static inline off_t map_table_offset(int index){
return (off_t) index * 4;
}

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock extend_lock;
    off_t pos;
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode_disk *inode_disk, off_t pos) 
{ 
  block_sector_t result_sec; 
  if (pos < inode_disk->length){
    struct inode_indirect_block *ind_block;
    struct sector_location sec_loc;
    locate_byte(pos, &sec_loc);
    //printf("sec_loc directness:%d\n", sec_loc.directness); 
    switch(sec_loc.directness){
      case NORMAL_DIRECT:
      //printf("sec_loc index1:%d, pos:%d, length:%d\n", sec_loc.index1, pos, inode_disk->length);
      result_sec = inode_disk->direct_map_table[sec_loc.index1];
      break;
      case INDIRECT:
      ind_block = malloc(BLOCK_SECTOR_SIZE);
      bc_read(inode_disk->indirect_block_sec, ind_block, 0, BLOCK_SECTOR_SIZE, 0);
      result_sec = ind_block->map_table[sec_loc.index1];
      free(ind_block);
      break;
      case DOUBLE_INDIRECT:
      ind_block = malloc(BLOCK_SECTOR_SIZE);
      bc_read(inode_disk->double_indirect_block_sec, ind_block, 0, BLOCK_SECTOR_SIZE, 0);
      bc_read(ind_block->map_table[sec_loc.index1], ind_block, 0, BLOCK_SECTOR_SIZE, 0);
      result_sec = ind_block->map_table[sec_loc.index2];
      free(ind_block);
      break;
      case OUT_LIMIT:
      result_sec = 0;
      break;
    }
    return result_sec;
  }
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, uint32_t is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;
  ASSERT (length >= 0);
  //struct inode *inode = inode_open(sector);
  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);
  //printf("inode length:%d, sector : %d\n", length, sector);
  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
    
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->indirect_block_sec = 0;
      disk_inode->double_indirect_block_sec = 0;
      disk_inode->is_dir = is_dir;
      if (length > 0) 
        {
         // lock_acquire(&inode->extend_lock);
          inode_update_file_length(disk_inode, 0, length - 1);
          bc_write (sector, disk_inode, 0, BLOCK_SECTOR_SIZE, 0); 
          // lock_release(&inode->extend_lock);
         //  inode_close(inode);
        } 
      free (disk_inode);
      success = true; 
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;
  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }
  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;
  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  inode->pos = 0;
  lock_init(&inode->extend_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
    //struct inode_disk *disk_inode = malloc(512);
    //struct inode *i = inode_open(0);
  //get_disk_inode(i, disk_inode);
  //printf("%d??\n", disk_inode->direct_map_table[0] );
  //inode_close(i);
  //free(disk_inode);
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{


  /* Ignore null pointer. */
  if (inode == NULL)
    return;
  /* Release resources if this was the last opener. */
  //printf("count:%d\n", inode->open_cnt);
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        { 
    
          struct inode_disk *disk_inode = malloc(BLOCK_SECTOR_SIZE);
          get_disk_inode(inode, disk_inode);
                    free_map_release (inode->sector, 1);
          free_inode_sectors(disk_inode);

          free(disk_inode);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{

  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;
  struct inode_disk *disk_inode = malloc(BLOCK_SECTOR_SIZE);
  get_disk_inode(inode, disk_inode);
    lock_acquire(&inode->extend_lock);
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (disk_inode, offset);
     // printf("read: inode sector:%d\n", inode->sector);
      //printf("inode sector:%d, sector_idx : %d\n", inode->sector, sector_idx);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = disk_inode->length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
          bc_read (sector_idx, buffer, bytes_read, chunk_size, sector_ofs);
          
        
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free(disk_inode);
lock_release(&inode->extend_lock);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{

  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;
  struct inode_disk *disk_inode = malloc(BLOCK_SECTOR_SIZE);
  if(disk_inode == NULL)
  return 0;
  get_disk_inode(inode, disk_inode);

  if (inode->deny_write_cnt){
   if(!inode_is_dir(inode))
    return 0;
    return -1;
  }
  
  int old_length = disk_inode->length;
  int write_end = offset + size -1;
 lock_acquire(&inode->extend_lock);
  if(write_end > old_length - 1){
    //printf("length update, write_end:%d\n", write_end);
inode_update_file_length(disk_inode, old_length, write_end);
  }
    

     bc_write(inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE, 0); 
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */

      block_sector_t sector_idx = byte_to_sector (disk_inode, offset);
      //printf("write: inode sector:%d\n", inode->sector);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = disk_inode->length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;


          bc_write (sector_idx, buffer, bytes_written, chunk_size, sector_ofs);


      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
 
  free(disk_inode);

        lock_release(&inode->extend_lock);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk *disk_inode = malloc(BLOCK_SECTOR_SIZE);
  get_disk_inode(inode, disk_inode);
  off_t length = disk_inode->length;
  free(disk_inode); 
  return length;
}

off_t inode_pos(const struct inode *inode){
  return inode->pos;
} 
void inode_set_pos(struct inode *inode, off_t pos){
  inode->pos = pos;
}

static bool get_disk_inode(const struct inode *inode, struct inode_disk *inode_disk){
  lock_acquire(&inode->extend_lock);
bc_read(inode->sector, inode_disk, 0, BLOCK_SECTOR_SIZE,0);
  lock_release(&inode->extend_lock);
return true;
}

static void locate_byte(off_t pos, struct sector_location *sec_loc){
off_t pos_sector = pos / BLOCK_SECTOR_SIZE;
if(pos_sector < DIRECT_BLOCK_ENTRIES){
sec_loc->directness = NORMAL_DIRECT;
sec_loc->index1 = pos_sector;
}
else if(pos_sector < DIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES){
sec_loc->directness = INDIRECT;
sec_loc->index1 = pos_sector - DIRECT_BLOCK_ENTRIES;
}
else if(pos_sector < DIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES * (INDIRECT_BLOCK_ENTRIES + 1)){
sec_loc->directness = DOUBLE_INDIRECT;
sec_loc->index1 = (pos_sector - (DIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES)) / INDIRECT_BLOCK_ENTRIES;
sec_loc->index2 = (pos_sector - (DIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES)) % INDIRECT_BLOCK_ENTRIES;
}
else{
sec_loc->directness = OUT_LIMIT;
}
}


static bool register_sector(struct inode_disk *inode_disk, block_sector_t new_sector, struct sector_location sec_loc){
  struct inode_indirect_block *new_block = NULL;
  block_sector_t sector_idx, idx;
  switch(sec_loc.directness){  
    case NORMAL_DIRECT:
    //printf("normal\n"); 
    inode_disk->direct_map_table[sec_loc.index1] = new_sector;
    break;
    case INDIRECT:
    if(inode_disk->indirect_block_sec == 0){
      if(!free_map_allocate(1, &sector_idx))
      return false;
      inode_disk->indirect_block_sec = sector_idx;
    }
    new_block = malloc (BLOCK_SECTOR_SIZE);
    bc_read(inode_disk->indirect_block_sec, new_block, 0, BLOCK_SECTOR_SIZE, 0);
    new_block->map_table[sec_loc.index1] = new_sector;
    bc_write(inode_disk->indirect_block_sec, new_block, 0, BLOCK_SECTOR_SIZE, 0);
    break;
    case DOUBLE_INDIRECT: 
    new_block = malloc (BLOCK_SECTOR_SIZE);
    if(inode_disk->double_indirect_block_sec == 0){
      if(!free_map_allocate(1, &sector_idx))
      return false;
      inode_disk->double_indirect_block_sec = sector_idx;
      int i;
      for(i=0;i<INDIRECT_BLOCK_ENTRIES;i++){
        if(!free_map_allocate(1, &idx)){
        free_map_release(1, sector_idx);
        inode_disk->double_indirect_block_sec = 0;
        return false;
        }
        new_block->map_table[i] = idx;
      }
      bc_write(sector_idx, new_block, 0, BLOCK_SECTOR_SIZE, 0);
    }
    bc_read(inode_disk->double_indirect_block_sec, new_block, 0, BLOCK_SECTOR_SIZE, 0);
    sector_idx = new_block->map_table[sec_loc.index1];
    bc_read(sector_idx, new_block, 0, BLOCK_SECTOR_SIZE, 0);
    new_block->map_table[sec_loc.index2] = new_sector;
    bc_write(sector_idx, new_block, 0, BLOCK_SECTOR_SIZE, 0);
    break;
    default:
    return false;
  }
  if(new_block != NULL)
    free(new_block);
  return true;
}

bool inode_update_file_length(struct inode_disk *inode_disk, off_t start_pos, off_t end_pos){

 // printf("%d %d %d\n", inode_disk->length, start_pos, end_pos);
  off_t size, offset;
  struct sector_location sec_loc;
  size = end_pos - start_pos + 1;
  offset = start_pos;
  void *zeroes = malloc(BLOCK_SECTOR_SIZE);
  memset(zeroes, 0, BLOCK_SECTOR_SIZE);
  while (size > 0) 
    {
      block_sector_t sector_idx;
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      off_t inode_left = end_pos + 1 - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;
      int chunk_size = size < min_left ? size : min_left;
      if(sector_ofs > 0){

      }
      else{
        if(free_map_allocate(1, &sector_idx)){
          locate_byte(offset, &sec_loc);
        register_sector(inode_disk, sector_idx, sec_loc);
        }
        else{
          free(zeroes);
          return false;
        }
        bc_write(sector_idx, zeroes, 0, BLOCK_SECTOR_SIZE, 0);
      }
            
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      inode_disk->length = offset;
    }
  free (zeroes);
    //printf("%d %d %d\n", inode_disk->length, start_pos, end_pos);
  return true;


}

static void free_inode_sectors(struct inode_disk *inode_disk){
  //printf("free\n");
  int i, j; 
  struct inode_indirect_block *ind_block_1, *ind_block_2;
  if(inode_disk->double_indirect_block_sec > 0){
   bc_read(inode_disk->double_indirect_block_sec, ind_block_1, 0, BLOCK_SECTOR_SIZE, 0);
   i = 0;
   while(ind_block_1->map_table[i] > 0){
     bc_read(ind_block_1->map_table[i], ind_block_2, 0, BLOCK_SECTOR_SIZE, 0);
     j = 0;
     while(ind_block_2->map_table[j] > 0){
       free_map_release(ind_block_2->map_table[j], 1);
       j++;
     }
     free_map_release(ind_block_1->map_table[i],1);
     i++;
   }
   free_map_release(inode_disk->double_indirect_block_sec,1);
  }

  if(inode_disk->indirect_block_sec > 0){
    bc_read(inode_disk->indirect_block_sec, ind_block_1, 0, BLOCK_SECTOR_SIZE, 0);
    i = 0;
    while(ind_block_1->map_table[i] > 0){
     free_map_release(ind_block_1->map_table[i], 1);
     i++;
   }
   free_map_release(inode_disk->indirect_block_sec, 1);
  }

  i = 0;
  while(inode_disk->direct_map_table[i] > 0){
    free_map_release(inode_disk->direct_map_table[i], 1);
    i++;
  }
}


bool inode_is_dir(const struct inode *inode){
  bool result;
  struct inode_disk *disk_inode = malloc(BLOCK_SECTOR_SIZE);
  get_disk_inode(inode, disk_inode);
  result = disk_inode->is_dir;
  free(disk_inode);
  return result;
}


int inode_cnt(struct inode *inode){
  return inode->open_cnt;
}