#include "vm/swap.h"
#include "vm/page.h"
#include "lib/kernel/hash.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include <bitmap.h>
#include "devices/block.h"

struct bitmap *b;

void swap_init(void){
 b = bitmap_create(1024);
}
void swap_in(size_t used_index, void* kaddr){
    struct block *block;
    int i;
    block = block_get_role(BLOCK_SWAP);
    void *buf, *buf_;
    buf = malloc(PGSIZE);
    buf_ = buf;
    bitmap_set(b, used_index, false);
    for(i=0;i<8;i++){
    block_read(block, used_index * 8 + i, buf);    
    buf += BLOCK_SECTOR_SIZE;
    }
   
    memcpy(kaddr, buf_, PGSIZE);
    free(buf_);

}
size_t swap_out(void* kaddr){
    size_t sector;
    struct block *block;
    void* addr = kaddr;
    int i;
    sector = bitmap_scan(b, 0, 1, false);
    if(sector == BITMAP_ERROR)
    return BITMAP_ERROR;
    block = block_get_role(BLOCK_SWAP);
    bitmap_mark(b, sector);
    for(i=0;i<8;i++){
    block_write(block, sector * 8 + i, addr);
    addr += BLOCK_SECTOR_SIZE;
    }
    return sector;
}