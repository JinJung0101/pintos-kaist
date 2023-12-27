/* vm.c: Generic interfalist_initce for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "include/lib/string.h"

/* for spt*/
unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
void page_kill (struct hash_elem *e, void *aux);
/* -------*/

struct list frame_table;
struct list_elem *clock_ref;
struct lock frame_table_lock;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_table);
	clock_ref = list_begin(&frame_table);
	lock_init(&frame_table_lock);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *page = (struct page *)malloc(sizeof(struct page));
		if (page ==  NULL) {
			return false;
		}
		bool (*page_initializer) (struct page *, enum vm_type, void *);

		switch (VM_TYPE(type)) {
			case VM_ANON:
				page_initializer = anon_initializer;
				break;
			case VM_FILE:
				page_initializer = file_backed_initializer;
				break;
			default:
				break;
		}
		uninit_new(page, upage, init, type, aux, page_initializer);
		/* TODO: Insert the page into the spt. */
		page->writable = writable;
		return spt_insert_page(spt, page);
	}
	else {
		goto err;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	/* TODO: Fill this function. */
	struct page *page = (struct page *)malloc(sizeof(struct page));
	struct hash_elem *e;

	page->va = pg_round_down(va);

	e = hash_find (&spt->spt_hash, &page->hash_elem);
	free(page);
	return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	/* TODO: Fill this function. */
	if (hash_insert (&spt->spt_hash, &page->hash_elem) == NULL) {
		return true;
	}
	else {
		return false;
	}
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	if (!hash_delete(&spt->spt_hash, &page->hash_elem)) {
		return;
	}
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */
	struct thread *curr = thread_current();

	lock_acquire(&frame_table_lock);
	for (clock_ref; clock_ref != list_end(&frame_table); clock_ref = list_next(clock_ref)) {
		victim = list_entry(clock_ref, struct frame, frame_elem);
		if (pml4_is_accessed(curr->pml4, victim->page->va)) {
			pml4_set_accessed(curr->pml4, victim->page->va, false);
		}
		else {
			lock_release(&frame_table_lock);
			return victim;
		}
	}

	struct list_elem *start = list_begin(&frame_table);

	for (start; start != list_end(&frame_table); start = list_next(start)) {
		victim = list_entry(start, struct frame, frame_elem);
		if (pml4_is_accessed(curr->pml4, victim->page->va)) {
			pml4_set_accessed(curr->pml4, victim->page->va, false);
		}
		else {
			lock_release(&frame_table_lock);
			return victim;
		}
	}
	lock_release(&frame_table_lock);
	ASSERT(clock_ref != NULL);
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	if (victim->page) {
		swap_out(victim->page);
	}
	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	
	/* TODO: Fill this function. */
	void *kva = palloc_get_page(PAL_USER);

	if (kva == NULL) {
		struct frame *victim = vm_evict_frame();
		victim->page = NULL;
		return victim;
	}
	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
	frame->kva = kva;
	frame->page = NULL;

	lock_acquire(&frame_table_lock);
	list_push_back(&frame_table, &frame->frame_elem);
	lock_release(&frame_table_lock);
	
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	// if (vm_alloc_page(VM_ANON|VM_MARKER_0, addr, 1)) {
	// 	thread_current()->stack_bottom -= PGSIZE;
	// }
	vm_alloc_page(VM_ANON | VM_MARKER_0, pg_round_down(addr), true);
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	if (is_kernel_vaddr(addr) || addr == NULL || !not_present) {
		return false;
	}
	// if ((USER_STACK - (1 << 20) <= rsp - 8 && rsp - 8 == addr && addr <= USER_STACK) || (USER_STACK - (1 << 20) <= rsp && rsp <= addr && addr <= USER_STACK)) {
	

	void *rsp = f->rsp;
	if(!user) {
		rsp = thread_current()->rsp_stack;
	}
	//USER_STACK - (1 << 20) = 스택 최대 크기 = 1MB
	//x86-64 PUSH 명령어는 스택 포인터를 조정하기 전에 액세스 권한을 확인하므로 스택 포인터 아래 8바이트의 페이지 장애가 발생할 수 있다.
	if ((USER_STACK - (1 << 20) <= rsp - 8 && rsp - 8 <= addr && addr <= USER_STACK)) {
		vm_stack_growth(addr);
	}
	page = spt_find_page(spt, addr);
	if(page == NULL) {
		return false;
	}
	if (write && (!page->writable)) { 
		return false;
	}
	if (!vm_do_claim_page(page)) {
		return false;
	}
	return true;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	page = spt_find_page(&thread_current()->spt, va);
	if (page == NULL) {
		return false;
	}
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	struct thread *cur = thread_current();
	if (install_page(page->va, frame->kva, page->writable)) {
		return swap_in (page, frame->kva);
	}
	else {
		return false;
	}
}


/* Returns a hash value for page p. */
unsigned 
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
	const struct page *p = hash_entry(p_, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof(p->va));
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct page *a = hash_entry (a_, struct page, hash_elem);
  const struct page *b = hash_entry (b_, struct page, hash_elem);
  return a->va < b->va;
}

void
page_kill (struct hash_elem *e, void *aux) {
	struct page *page = hash_entry(e, struct page, hash_elem);
	destroy(page);
	free(page);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->spt_hash, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
// bool
// supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
// 		struct supplemental_page_table *src UNUSED) {
// 	struct hash_iterator i;
// 	hash_first(&i, &src->spt_hash);
	
// 	while (hash_next(&i)) {
// 		struct page *parent_page = hash_entry (hash_cur(&i), struct page, hash_elem);
// 		enum vm_type type = parent_page->operations->type;
// 		void *upage = parent_page->va;
// 		bool writable = parent_page->writable;

// 		if (type == VM_UNINIT) {
// 			vm_initializer *init = parent_page->uninit.init;
// 			void *aux = parent_page->uninit.aux;
// 			// struct segment *file_loader = (struct segment *)aux;
//             // struct segment *new_file_loader = malloc(sizeof(struct segment));
//             // memcpy(new_file_loader, aux, sizeof(struct segment));
//             // new_file_loader->file = file_duplicate(file_loader->file);
			
// 			vm_alloc_page_with_initializer(VM_ANON, upage, writable, init, aux);
// 			// free(new_file_loader);
// 			continue;
// 		}
// 		if (type == VM_FILE) {
// 			struct segment *file_aux = malloc(sizeof(struct segment));
// 			file_aux->file = parent_page->file.file;
// 			file_aux->ofs = parent_page->file.ofs;
// 			file_aux->page_read_bytes = parent_page->file.page_read_bytes;
// 			file_aux->page_zero_bytes = parent_page->file.page_zero_bytes;
// 			if (!vm_alloc_page_with_initializer(type, upage, writable, NULL, file_aux)) {
// 				// free(file_aux);
// 				return false;
// 			}
// 			// free(file_aux);

// 			struct page *file_page = spt_find_page(dst, upage);
// 			file_backed_initializer(file_page, type, NULL);
// 			file_page->frame = parent_page->frame;
// 			pml4_set_page(thread_current()->pml4, file_page->va, parent_page->frame->kva, parent_page->writable);
// 			continue;
// 		}
// 		if (!vm_alloc_page(type, upage, writable)) {
// 			return false;
// 		}
// 		if (!vm_claim_page(upage)) {
// 			return false;
// 		}
// 		struct page *child_page = spt_find_page(dst, upage);
// 		memcpy(child_page->frame->kva, parent_page->frame->kva, PGSIZE);
// 	}
// 	return true;
// }

bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	struct hash_iterator i;
	hash_first(&i, &src->spt_hash);
	
	while (hash_next(&i)) {
		struct page *parent_page = hash_entry (hash_cur (&i), struct page, hash_elem);
		enum vm_type type = page_get_type(parent_page);
		enum vm_type now_type = parent_page->operations->type;
		void *upage = parent_page->va;
		bool writable = parent_page->writable;

		if (now_type == VM_UNINIT) {
			vm_initializer *init = parent_page->uninit.init;
			void *aux = parent_page->uninit.aux;
			struct segment *file_loader = (struct segment *)aux;
            struct segment *new_file_loader = malloc(sizeof(struct segment));
            memcpy(new_file_loader, aux, sizeof(struct segment));
            new_file_loader->file = file_duplicate(file_loader->file);
			
			if (!vm_alloc_page_with_initializer(type, upage, writable, init, new_file_loader)) {
				free(new_file_loader);
				return false;
			}

			if (!vm_claim_page(upage)) {
				free(new_file_loader);
				return false;
			}
		}
		else if (now_type == VM_FILE) {
			struct segment *file_aux = malloc(sizeof(struct segment));
			file_aux->file = parent_page->file.file;
			file_aux->ofs = parent_page->file.ofs;
			file_aux->page_read_bytes = parent_page->file.page_read_bytes;
			file_aux->page_zero_bytes = parent_page->file.page_zero_bytes;
			if (!vm_alloc_page_with_initializer(type, upage, writable, NULL, file_aux)) {
				free(file_aux);
				return false;
			}
	
			struct page *file_page = spt_find_page(dst, upage);
			file_backed_initializer(file_page, type, NULL);
			file_page->frame = parent_page->frame;
			pml4_set_page(thread_current()->pml4, file_page->va, parent_page->frame->kva, parent_page->writable);
			// continue;
		}
		else {
			if (!vm_alloc_page(type, upage, writable)) {
				return false;
			}
			if (!vm_claim_page(upage)) {
				return false;
			}
			struct page *child_page = spt_find_page(dst, upage);
			if (child_page ==  NULL) {
				return false;
			}
			memcpy(child_page->frame->kva, parent_page->frame->kva, PGSIZE);
		}
	}
	return true;
}


/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_clear(&spt->spt_hash, page_kill);
}

