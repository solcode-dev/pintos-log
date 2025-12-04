/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/mmu.h"	  // 1204 추가
#include "filesys/file.h"	  // 1204 추가
#include "userprog/syscall.h" // file_lock 사용
#include <string.h>			  // memset

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

static bool file_backed_swap_in(struct page *page, void *kva)
{
	struct file_page *file_page = &page->file;

	if (page == NULL || kva == NULL)
		return false;

	// TODO: swap disk에 있는지 확인하고, 있으면 거기서 읽어온다
	// 현재는 항상 원본 파일에서 읽어온다

	// 원본 파일에서 읽어온다
	lock_acquire(&file_lock);
	file_seek(file_page->file, file_page->offset);
	off_t read_bytes = file_read(file_page->file, kva, file_page->page_read_bytes);
	lock_release(&file_lock);

	// 읽기 실패 시
	if (read_bytes != (off_t)file_page->page_read_bytes)
		return false;

	// PGSIZE에서 남는 부분을 0으로 채운다
	memset(kva + file_page->page_read_bytes, 0, PGSIZE - file_page->page_read_bytes);

	return true;
}

static bool file_backed_swap_out(struct page *page)
{
	// dirty에 따라 달라짐
	// dirty하면
	// → mmap은 write back
	// → 실행파일(code, data, rodata-상수/문자열/리터럴)은 원본. 수정하면 안되니까 swap disk.
	// dirty안하면
	// → 그냥 버림

	struct file_page *file_page UNUSED = &page->file;

	if (page == NULL || page->frame == NULL)
		return false;

	// 1. dirty 비트 확인
	bool is_dirty = pml4_is_dirty(thread_current()->pml4, page->va);

	// 2. dirty하면 처리
	if (is_dirty) {
		// writable인 경우 (mmap): 원본 파일에 write back
		if (page->writable) {
			lock_acquire(&file_lock);
			file_write_at(file_page->file, page->frame->kva, file_page->page_read_bytes,
						  file_page->offset);
			lock_release(&file_lock);
		}
		// read-only인 경우 (실행파일 code, data, rodata): swap disk에 써야 함
		else {
			// TODO: swap disk에 쓰기
			// 실행파일은 수정되면 안 되니까, dirty가 발생한 경우 swap disk로
		}
		// dirty 비트를 clean으로 설정
		pml4_set_dirty(thread_current()->pml4, page->va, false);
	}

	// 3. 페이지 테이블 엔트리에서 present 비트를 0으로 설정
	pml4_clear_page(thread_current()->pml4, page->va);

	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void file_backed_destroy(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
}

/* Do the mmap */
void *do_mmap(void *addr, size_t length, int writable, struct file *file, off_t offset)
{
}

/* Do the munmap */
void do_munmap(void *addr)
{
}
