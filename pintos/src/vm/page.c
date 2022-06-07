#include "vm/page.h"
#include "lib/kernel/hash.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
//#include "filesys/file.h"

static unsigned vm_hash_func (const struct hash_elem *e, void *aux);
static bool vm_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux);
static void vm_destroy_func(struct hash_elem *e, void *aux);


void vm_init (struct hash *vm){
hash_init(vm, vm_hash_func, vm_less_func, NULL);
}

static unsigned vm_hash_func (const struct hash_elem *e, void *aux){
struct vm_entry *v = hash_entry(e, struct vm_entry, elem);
return hash_int(v->vaddr);
}

static bool vm_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux){
struct vm_entry *v1 = hash_entry(a, struct vm_entry, elem);
struct vm_entry *v2 = hash_entry(b, struct vm_entry, elem);
return v2->vaddr > v1->vaddr ? 1 : 0;

}

static void vm_destroy_func(struct hash_elem *e, void *aux){
struct vm_entry *v = hash_entry(e, struct vm_entry, elem);
free(v);
}

bool insert_vme (struct hash *vm, struct vm_entry *vme){
struct hash_elem *e = hash_insert(vm, &vme->elem);
if(e == NULL)
return true;
return false;
}

bool delete_vme (struct hash *vm, struct vm_entry *vme){
struct hash_elem *e = hash_delete(vm, &vme->elem);
if(e == NULL)
return false; 
free(vme);
return true;
}

struct vm_entry *find_vme (void *vaddr){
  size_t i;
  struct hash *h = &thread_current()->vm;
  uint32_t pa = pg_round_down(vaddr);
  for (i = 0; i < h->bucket_cnt; i++) 
    {
      struct list *bucket = &h->buckets[i];
      struct list_elem *elem, *next;

      for (elem = list_begin (bucket); elem != list_end (bucket); elem = list_next (elem)) 
        {
          
          struct hash_elem *e = list_entry (elem, struct hash_elem, list_elem);
          struct vm_entry *v = hash_entry (e, struct vm_entry, elem);
          if(v->vaddr == pa)
          return v;
        }

    }
    return NULL;
}

void vm_destroy (struct hash *vm){
    hash_destroy (vm, vm_destroy_func); 
}

bool load_file (void* kaddr, struct vm_entry *vme){ 
  file_read_at(vme->file, kaddr, vme->read_bytes, vme->offset); 
  memset (kaddr + vme->read_bytes, 0, vme->zero_bytes);
  return true;
}





