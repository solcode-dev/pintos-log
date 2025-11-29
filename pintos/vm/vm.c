/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void)
{
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */

	/* SPT 초기화 테스트 */
	printf("\n========== SPT 초기화 테스트 ==========\n");
	struct supplemental_page_table test_spt;
	supplemental_page_table_init(&test_spt);
	printf("SPT 초기화 완료:\n");
	printf("  - bucket_cnt: %zu\n", test_spt.pages.bucket_cnt);
	printf("  - elem_cnt: %zu\n", test_spt.pages.elem_cnt);
	printf("  - hash function: %s\n", test_spt.pages.hash != NULL ? "설정됨" : "NULL");
	printf("  - less function: %s\n", test_spt.pages.less != NULL ? "설정됨" : "NULL");
	printf("========================================\n\n");
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type page_get_type(struct page *page)
{
	int ty = VM_TYPE(page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE(page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);
static uint64_t page_hash(const struct hash_elem *elem, void *aux);
static bool page_less(const struct hash_elem *elem_a, const struct hash_elem *elem_b, void *aux);

/* page 생성 + spt에 등록
 * UNINIT → ANON/FILE 변환한다. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *va, bool writable,
									vm_initializer *init, void *aux)
{

	ASSERT(VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;

	// 1. spt에 이미 등록된 페이지인지 확인한다
	if (spt_find_page(spt, va) != NULL)
		return false;

	// 2. struct page를 할당한다
	struct page *page = malloc(sizeof(struct page));
	if (page == NULL)
		return false;

	// 3. type에 맞는 initializer를 선택한다. (이게 최종 목표 타입)
	// 함수포인터: 리턴타입이 bool이고, 매개변수를 3개 받는 포인터 initializer를 선언함
	bool (*initializer)(struct page *, enum vm_type, void *kva);
	switch (VM_TYPE(type)) {
		case VM_ANON:
			initializer = anon_initializer;
			break;
		case VM_FILE:
			initializer = file_backed_initializer;
			break;
		default:
			goto err;
	}

	// 4. uninit 페이지 생성 (VM_UNINIT으로 설정됨)
	uninit_new(page, va, init, type, aux, initializer);

	// 5. spt에 삽입
	if (!spt_insert_page(spt, page))
		goto err;

	// 6. 성공
	return true;

err:
	free(page);
	return false;
}

/* spt에서 va로 페이지를 찾아 반환하는 함수.
 * 찾는 페이지가 없으면 NULL을 반환. */
struct page *spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED)
{
	// 1. 페이지 경계로 va를 내린다
	struct page page;
	page.va = pg_round_down(va);

	// 2. 해시 테이블에서 검색한다
	struct hash_elem *elem = hash_find(&spt->pages, &page.hash_elem);

	// 3. 찾았으면 page 구조체를 반환, 못 찾았으면 NULL
	if (elem != NULL)
		return hash_entry(elem, struct page, hash_elem);

	return NULL;
}

// spt에 페이지 추가
bool spt_insert_page(struct supplemental_page_table *spt UNUSED, struct page *page UNUSED)
{
	// 1. 이미 같은 va가 있는지 확인한다
	if (spt_find_page(spt, page->va) != NULL)
		return false;  // 이미 존재하면 실패

	// 2. hash_insert로 추가한다
	hash_insert(&spt->pages, &page->hash_elem);

	// 3. 성공
	return true;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	hash_delete(&spt->pages, &page->hash_elem);
	vm_dealloc_page(page);
}

/* Get the struct frame, that will be evicted. */
static struct frame *vm_get_victim(void)
{
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *vm_evict_frame(void)
{
	struct frame *victim UNUSED = vm_get_victim();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *vm_get_frame(void)
{
	struct frame *frame = NULL;
	/* TODO: Fill this function. */

	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void vm_stack_growth(void *addr UNUSED)
{
}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED)
{
}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED, bool user UNUSED,
						 bool write UNUSED, bool not_present UNUSED)
{
	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	return vm_do_claim_page(page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page)
{
	destroy(page);
	free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED)
{
	struct page *page = NULL;
	/* TODO: Fill this function */

	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool vm_do_claim_page(struct page *page)
{
	struct frame *frame = vm_get_frame();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */

	return swap_in(page, frame->kva);
}

/* 해시테이블을 초기화하는 함수.
 * 새 프로세스가 시작될 때 initd, __do_fork에서 호출된다. */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED)
{
	hash_init(&spt->pages, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
								  struct supplemental_page_table *src UNUSED)
{
	return false;
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}

// va로 해시 키를 만들어서 반환하는 함수
static uint64_t page_hash(const struct hash_elem *elem, void *aux)
{
	const struct page *pPage = hash_entry(elem, struct page, hash_elem);
	return hash_bytes(&pPage->va, sizeof pPage->va);
}

/* page가 같은지, 혹은 순서가 앞서는지를 va를 기준으로 판단하는 함수
 * a가 b보다 더 크면 true를 반환한다. */
static bool page_less(const struct hash_elem *elem_a, const struct hash_elem *elem_b, void *aux)
{
	const struct page *page_a = hash_entry(elem_a, struct page, hash_elem);
	const struct page *page_b = hash_entry(elem_b, struct page, hash_elem);
	return page_a->va > page_b->va;
}
