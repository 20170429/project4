#include "vm/frame.h"
#include "threads/thread.h"
#include <list.h>
#include "threads/vaddr.h"
#include "vm/swap.h"
#include "userprog/pagedir.h"


void lru_list_init(void){
list_init(&lru_list);
lock_init(&lru_list_lock);
lru_clock = NULL;
}

void add_page_to_lru_list(struct page* page){
list_push_back(&lru_list, &page->lru);
}

void del_page_from_lru_list(struct page* page){
list_remove(&page->lru);
}

static struct list_elem* get_next_lru_clock(void){
 struct list_elem *e;
 if(lru_clock == NULL){
   lru_clock = list_begin(&lru_list);
   return lru_clock;
 }
 e = list_next(lru_clock);
 if(e == list_end(&lru_list)){
 lru_clock = NULL;
 return NULL;
 }
 lru_clock = e;
 return lru_clock;
}

struct page* alloc_page(enum palloc_flags flags){
void *p;
while(p == NULL){
p = palloc_get_page(flags);
if(p == NULL)
try_to_free_pages(flags);
}

struct page *page = (struct page *) malloc(sizeof (struct page));
//struct vm_entry *vme = (struct vm_entry *) malloc(sizeof (struct vm_entry));
page->kaddr =p;
//page->vme = vme;
page->thread = thread_current();
add_page_to_lru_list(page);
return page;

}

void free_page(void *kaddr){
struct list_elem *e;
struct page *page;
struct vm_entry *vme;
    for (e = list_begin (&lru_list); e != list_end (&lru_list);
      )
    {
      page = list_entry (e, struct page, lru);
       e = list_next (e);
      if(page->kaddr == kaddr){
      __free_page(page);
      break;
      }
    } 
}

void free_all(void){
    struct list_elem *e;
    struct page *page;
    struct vm_entry *vme;
    for (e = list_begin (&lru_list); e != list_end (&lru_list);
      )
    {

      page = list_entry (e, struct page, lru);
       e = list_next (e);
      if(page->thread->tid == thread_tid()){
           del_page_from_lru_list(page);
      }
      }
}

void __free_page(struct page* page){
del_page_from_lru_list(page);
palloc_free_page(page->kaddr);
}

void try_to_free_pages (enum palloc_flags flags){
    struct list_elem *e = get_next_lru_clock();
    size_t swap_slot;
    if(e != NULL){
    struct page *page = list_entry(e, struct page, lru);
    if(pagedir_is_accessed(page->thread->pagedir, page->vme->vaddr))
    pagedir_set_accessed(page->thread->pagedir, page->vme->vaddr, false);
    else{  //victim selected
    //printf("victim  \n");
        switch (page->vme->type){
        case VM_BIN: 
               //printf("bin\n");

          if(pagedir_is_dirty(page->thread->pagedir, page->vme->vaddr)){
          swap_slot = swap_out(page->kaddr);
          if(swap_slot == -1)
            break;
          else
             page->vme->swap_slot = swap_slot;
          page->vme->type = VM_ANON;   
          }
          pagedir_clear_page(page->thread->pagedir, page->vme->vaddr);  
          free_page(page->kaddr);
          break;
        case VM_FILE:
          if(pagedir_is_dirty(page->thread->pagedir, page->vme->vaddr))
          file_write_at(page->vme->file, pagedir_get_page(page->thread->pagedir, page->vme->vaddr) , page->vme->read_bytes, page->vme->offset);
          pagedir_clear_page(page->thread->pagedir, page->vme->vaddr);  
          free_page(page->kaddr);
          break;
        case VM_ANON:
        //printf("anon\n");
          swap_slot = swap_out(page->kaddr);
          if(swap_slot == -1)
            break;
          else
             page->vme->swap_slot = swap_slot;  
          pagedir_clear_page(page->thread->pagedir, page->vme->vaddr);  
          free_page(page->kaddr);
          break;
    }
    
    }
    }
}