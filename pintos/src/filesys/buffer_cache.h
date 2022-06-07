#include "devices/block.h"
#include <stdbool.h>
#include "threads/synch.h"
#include "filesys/off_t.h"

struct semaphore bh_sema;

struct buffer_head{
    struct inode* inode;
    bool dirty;
    bool used;
    struct lock lock;
    block_sector_t sector;
    int clock_bit;
    void* data;
};


void write_behind(void);
bool bc_read(block_sector_t sector_idx, void *buffer, off_t bytes_read, int chunk_size, int sector_ofs);
bool bc_write(block_sector_t sector_idx, void *buffer, off_t bytes_written, int chunk_size, int sector_ofs);
void bc_init(void);
void bc_term(void);
struct buffer_head* bc_select_victim(void);

struct buffer_head* bc_lookup(block_sector_t sector);

void bc_flush_entry(struct buffer_head *p_flush_entry);

void bc_flush_all_entries(void);