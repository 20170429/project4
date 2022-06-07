#include <string.h>>
#include "lib/kernel/hash.h"
#include "devices/block.h"

struct hash dentry_cache;

struct dc_entry
{
    char *absolute_path;        // file의 절대 경로.
    block_sector_t inumber;     // file에 대응되는 inode의 sector number.
    struct hash_elem elem;      // hash table에서 활용되는 hash_elem.
};

static unsigned dc_hash_func (const struct hash_elem *e, void *aux);
static bool dc_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux);
void dc_init (void);
bool insert_dce (struct hash *dc, struct dc_entry *dce);
bool delete_dce (struct hash *dc, struct dc_entry *dce);
struct dc_entry *find_dce (char *path);
static void dc_destroy_func (struct hash_elem *e, void *aux);
void dc_destroy (struct hash *dc);