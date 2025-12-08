/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "userprog/process.h"
#include <string.h>

extern struct lock file_lock; // syscall.c 것을 사용

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);
static bool lazy_load_mmap(struct page *page, void *aux);

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

// VM타입 변경 및 file_page 구조체 값 넣기
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

	page->file = *file_page;

	return true;
}

// 스왑 아웃된 페이지를 다시 메모리로 가져온다.
static bool file_backed_swap_in(struct page *page, void *kva)
{
	struct file_page *file_page = &page->file;
	struct file *file = file_page->file;
	off_t offset = file_page->offset;
	size_t page_read_bytes = file_page->page_read_bytes;

	// 파일에서 데이터 읽기
	lock_acquire(&file_lock);
	int read_result = file_read_at(file, kva, page_read_bytes, offset);
	lock_release(&file_lock);
	if (read_result != (int)page_read_bytes) {
		return false;
	}

	// 남은 바이트를 0으로 채우기
	if (read_result < PGSIZE) {
		memset(kva + page_read_bytes, 0, PGSIZE - page_read_bytes);
	}

	return true;
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
}

// length 바이트만큼의 파일을 offset 바이트 지점부터 시작해서,
// 프로세스의 가상 주소 공간 내 addr 주소에 매핑한다
// 만약 파일의 길이가 PGSIZE의 배수가 아니라면,
// 마지막 매핑된 페이지의 일부 바이트는 파일 끝을 넘어 “튀어나오게” 되는데
// 이런 바이트들은 페이지 폴트로 해당 페이지를 들여올 때 0으로 채우고,
// 디스크로 다시 쓸 때는 버린다
// 매핑에 성공하면 파일이 매핑된 시작 가상 주소를 반환하고,
// 실패하면 매핑에 사용할 수 없는 주소 값인 NULL을 반환해야 한다
void *do_mmap(void *addr, size_t length, int writable, struct file *file, off_t offset)
{
	off_t file_len = file_length(file);
	if (file_len == 0 || file_len <= offset || length == 0)
		return NULL;

	if (NULL == addr || is_kernel_vaddr(addr))
		return NULL;

	// 사용하려는 주소가 다 비어있는지 체크
	void *start_addr = pg_round_down(addr);
	void *end_addr = pg_round_up(addr + length);
	size_t total_bytes = (uint64_t)end_addr - (uint64_t)start_addr;
	size_t page_count = total_bytes / PGSIZE;
	for (size_t i = 0; i < page_count; i++) {
		void *check_addr = start_addr + (i * PGSIZE);
		// 스택 영역과 겹치는가 (스택 최초 setup시 PGSIZE만큼만 spt에 등록되므로 1MB 확인)
		if (check_addr >= (void *)(USER_STACK - (1 << 20)))
			return NULL;

		// 이미 매핑되었는가
		if (NULL != spt_find_page(&(thread_current()->spt), check_addr))
			return NULL;
	}

	for (size_t i = 0; i < page_count; i++) {
		void *page_addr = start_addr + (i * PGSIZE);
		size_t remaining = length - (i * PGSIZE);
		size_t page_read_bytes = remaining < PGSIZE ? remaining : PGSIZE;

		//페이지별 aux 생성
		struct file_page *aux = malloc(sizeof(struct file_page));
		if (NULL == aux)
			return NULL;

		*aux = (struct file_page){
			.file = file,
			.offset = offset + (i * PGSIZE),
			.page_read_bytes = page_read_bytes //
		};

		if (!vm_alloc_page_with_initializer(VM_FILE, page_addr, writable, file_backed_swap_in,
											aux)) {
			free(aux);
			aux = NULL;
			return NULL;
		};
	}

	return start_addr;
}

/* Do the munmap */
void do_munmap(void *addr)
{
}
