#include "buffer_cache.h"
#include "filesys/inode.h"
#include "filesys/filesys.h"

#define BUFFER_CACHE_ENTRY_NB 64

uint8_t *p_buffer_cache;

uint8_t *current_p;

struct buffer_head buffers[64];

int entry;

int clock_hand, period;


void write_behind(void){
    int i;
    for(i=0;i<64;i++){
        if(buffers[i].used && buffers[i].dirty){
        block_write(fs_device, buffers[i].sector, buffers[i].data);
        }
    }
    period = 0;
}

static int buffer_idx(void){
    int i;
    for(i=0;i<64;i++){
        if(buffers[i].sector == -1)
        return i;
    }
}


void bc_init(void){
int i;
entry = 0;
clock_hand = 0;
period = 0;
p_buffer_cache = calloc(64 ,512);
for(i=0;i<64;i++){
//buffers[i].used = false;
buffers[i].sector = -1;
buffers[i].used = false;
}
current_p = p_buffer_cache;
sema_init(&bh_sema, 1);
}

void bc_term(void){
    bc_flush_all_entries();
    free(p_buffer_cache);
}

bool bc_read(block_sector_t sector_idx, void *buffer, off_t bytes_read, int chunk_size, int sector_ofs){
  // printf("current_p:%x, sector:%d, empty_entry:%d\n",current_p, sector_idx,empty_entry);

 struct buffer_head *bh;
 //printf("this is read and empty_entry is %d\n", entry);
  bh = bc_lookup(sector_idx);
  int index;
  if(bh == NULL){
   if(entry != BUFFER_CACHE_ENTRY_NB){
       index = buffer_idx();
       bh = &buffers[index];
       bh->sector = sector_idx;
       bh->inode = inode_open(sector_idx);
       bh->data = p_buffer_cache + index * 512;
       bh->clock_bit = 1;
       bh->dirty = false;
       lock_init(&bh->lock);
       

   }
   else{
      bh = bc_select_victim();
      bh->sector = sector_idx;
      bh->inode = inode_open(sector_idx);
      bh->dirty = false;
      bh->clock_bit = 1;
      lock_init(&bh->lock);
      entry--;
   }
     lock_acquire(&bh->lock);
     block_read(fs_device, sector_idx, bh->data);   
     entry++;
     lock_release(&bh->lock);
  }
  bh->used = true;
  memcpy(buffer + bytes_read, bh->data + sector_ofs, chunk_size);
  period++;
  return 1;
}



bool bc_write(block_sector_t sector_idx, void *buffer, off_t bytes_written, int chunk_size, int sector_ofs){
    if(period >= 400)
write_behind();
int index;
    bool success = false;
    struct buffer_head *bh;
    bh = bc_lookup(sector_idx);
    if(bh == NULL){
   if(entry != BUFFER_CACHE_ENTRY_NB){
       index = buffer_idx();
       bh = &buffers[index];
       bh->sector = sector_idx;
       bh->inode = inode_open(sector_idx);
       bh->data = p_buffer_cache + index * 512;
       bh->clock_bit = 1;
       lock_init(&bh->lock);
       

   }
   else{
      bh = bc_select_victim();
      bh->sector = sector_idx;
      bh->inode = inode_open(sector_idx);
      bh->clock_bit = 1;
      lock_init(&bh->lock);
      entry--;
   }
    
     entry++;
  }
  //block_write(fs_device, sector_idx, buffer);
  memcpy(bh->data + sector_ofs, buffer + bytes_written, chunk_size);
  bh->dirty = true;
  bh->used = true;
  bh->clock_bit = 1;
    return true;
}
struct buffer_head* bc_select_victim(void){
    struct buffer_head *bh;
while(1){
    if(clock_hand >= 64)
    clock_hand = 0;
    if(!buffers[clock_hand].clock_bit && buffers[clock_hand].sector != 0)
    break;
    buffers[clock_hand++].clock_bit = 0;
}
bh = &buffers[clock_hand];
if(bh->dirty){
bc_flush_entry(bh);
}
inode_close(bh->inode);
bh->inode = NULL;
period++;
return bh;
}

struct buffer_head* bc_lookup(block_sector_t sector){
int i;
for(i=0;i<64;i++){
    //printf("buffer sector[%d] : %d\n",i,buffers[i].sector);
if(buffers[i].sector == sector)
return &buffers[i];
}

return NULL;
}

void bc_flush_entry(struct buffer_head *p_flush_entry){
block_write(fs_device, p_flush_entry->sector, p_flush_entry->data);
}

void bc_flush_all_entries(void){
 int i;
 for(i=0;i<64;i++){
     if(buffers[i].dirty && buffers[i].sector != 0)
     bc_flush_entry(&buffers[i]);
     if(buffers[i].used)
     inode_close(buffers[i].inode);
 }
}