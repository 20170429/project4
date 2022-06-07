#include "filesys/dentry_cache.h"

static unsigned 
dc_hash_func (const struct hash_elem *e, void *aux)
{
    struct dc_entry *dce_elem = hash_entry (e, struct dc_entry, elem);
    
    return hash_string (dce_elem->absolute_path);
}

static bool 
dc_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
    struct dc_entry *dce_a = hash_entry (a, struct dc_entry, elem);
    struct dc_entry *dce_b = hash_entry (b, struct dc_entry, elem);
    
    return dce_a->absolute_path < dce_b->absolute_path;
}

void
dc_init (void)
{
    hash_init (&dentry_cache, dc_hash_func, dc_less_func, NULL);
}

bool 
insert_dce (struct hash *dc, struct dc_entry *dce)
{
    struct hash_elem *e = hash_insert (dc, &dce->elem);
    
    /* dentry_cahce에 동일한 dc_entry가 존재하지 않는 경우. */
    if (e == NULL)
        return true;
    
    /* 이미 동일한 dc_entry가 존재하는 경우 */
    return false;
}

bool 
delete_dce (struct hash *dc, struct dc_entry *dce)
{
    struct hash_elem *e = hash_delete(dc, &dce->elem);
    
    /* dentry_cahce에 제거할 dc_entry가 존재하지 않는 경우. */
    if (e == NULL)
        return false; 
    
    /* 제거할 dc_entry가 존재하는 경우. */
    free (dce);
    return true;
}

struct dc_entry *
find_dce (char *path)
{   int i;
struct list_elem *bucket_e;
    for (i = 0; i < dentry_cache.bucket_cnt; i++) 
    {
        /* hash value에 대응되는 bucket에서 path를 member로 가진 dc_entry가 있는지 찾는다. */
        struct list *target_bucket = &dentry_cache.buckets[i];

        for (bucket_e = list_begin (target_bucket); bucket_e != list_end (target_bucket); bucket_e = list_next (bucket_e)) 
        {
            struct hash_elem *hash_e = list_entry (bucket_e, struct hash_elem, list_elem);
            struct dc_entry *dce = hash_entry (hash_e, struct dc_entry, elem);
            
            if (strcmp (dce->absolute_path, path) == 0)
                return dce;
        }
    }

    return NULL;
}

static void 
dc_destroy_func (struct hash_elem *e, void *aux)
{
    struct dc_entry *dce = hash_entry(e, struct dc_entry, elem);
    
    free(dce);
}

void 
dc_destroy (struct hash *dc)
{
    hash_destroy (dc, dc_destroy_func); 
}