/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void vm_file_init(void)
{
}

/* Initialize the file backed page */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva)
{
	if (page == NULL || kva == NULL || type != VM_FILE)
		return false;

	// 1. VM_FILE에 맞게 operations 변경
	page->operations = &file_ops;

	// 2. file_page 구조체 초기화
	struct file_page *aux = page->uninit.aux;
	struct file_page *file_page = &page->file;

	*file_page = (struct file_page){
		.offset = aux->offset,
		.file = aux->file,
		.page_read_bytes = aux->page_read_bytes,
	};

	return true;
}

/* Swap in the page by read contents from the file. */
static bool file_backed_swap_in(struct page *page, void *kva)
{
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool file_backed_swap_out(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void file_backed_destroy(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;

	if (page->frame != NULL) {
		// pte에서 매핑 제거
		pml4_clear_page(thread_current()->pml4, page->va);

		// 물리메모리도 제거
		palloc_free_page(page->frame->kva);

		// frame 구조체 해제
		free(page->frame);
		page->frame = NULL;
	}
}

/* Do the mmap */
void *do_mmap(void *addr, size_t length, int writable, struct file *file, off_t offset)
{
	// size_t read_bytes = 0;

	// vm_alloc_page_with_initializer()

	/*
	1. length보다 크거나 같을 때까지 += PGSIZE 해주면서 페이지 늘려주기
	2. 페이지마다 만들고, vm_alloc_page_with_initializer() 호출해서 파일 & offset 전달하기 (offset
	reopen하기)
	3. offset & read_bytes 값 업데이트하기
	4. init_aux로 file_page struct 전달할거고, init 함수는 lazy_load_segment, VM_TYPE은 VM_FILE로,
	upage는 page 시작 주소, writable 같이 전달하기
	*/
}

/* Do the munmap */
void do_munmap(void *addr)
{
	/*
	1. addr로 page 검색해서 찾기
	2. spt_remove_page 호출해서 해당 페이지 삭제하기
	*/
}
