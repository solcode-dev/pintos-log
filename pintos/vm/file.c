/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"

// 디버깅 켜기 (1) / 끄기 (0)
#define DEBUG_MODE 0

#if DEBUG_MODE
#define debug_print(...) printf(__VA_ARGS__)
#else
#define debug_print(...) /* 아무것도 안 함 */
#endif

/* 디버깅용 색상 매크로 */
#define R "\033[31m" // Red (에러, 위험)
#define G "\033[32m" // Green (성공, 완료)
#define Y "\033[33m" // Yellow (경고, 중요 정보)
#define B "\033[34m" // Blue (일반 정보)
#define M "\033[35m" // Magenta (함수 진입/종료 등 흐름)
#define C "\033[36m" // Cyan (변수 값 확인)
#define W "\033[0m"	 // White/Reset (색상 초기화 - 필수!)

static bool file_backed_swap_in(struct page *page, void *kva);
static void file_backed_destroy(struct page *page);
static bool lazy_load_file(struct page *page);
static void write_back(struct page *page);

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

	*file_page = (struct file_page){.offset = aux->offset,
									.file = aux->file,
									.page_read_bytes = aux->page_read_bytes,
									.index = aux->index,
									.pages_cnt = aux->pages_cnt};

	return true;
}

/* 페이지 내용을 파일에 다시 써서 스왑 아웃한다.
   페이지가 dirty인지 확인해서,
	 dirty가 아니라면 파일 내용을 굳이 수정할 필요가 없다.
	 스왑 아웃이 끝난 뒤에는 해당 페이지의 dirty 비트를 꺼 주어야 한다. */
static bool file_backed_swap_in(struct page *page, void *kva)
{
	struct file_page *file_page UNUSED = &page->file;

	off_t ofs = file_page->offset;
	size_t page_read_bytes = file_page->page_read_bytes;

	// 유효성 검사 (파일 포인터가 없으면 커널 패닉이나 에러가 날 수 있음)
	if (file_page->file == NULL) {
		debug_print(R "  [!!! CRITICAL !!!] File pointer is NULL. Lazy load will crash." W "\n");
		return false;
	}

	lock_acquire(&file_lock);
	int read_result = file_read_at(file_page->file, page->frame->kva, page_read_bytes, ofs);
	lock_release(&file_lock);

	if (read_result < PGSIZE) {
		page->file.page_read_bytes = read_result;
		memset(page->frame->kva + read_result, 0, PGSIZE - page_read_bytes);
	}

	// 디버깅용 : 794개 읽은 게 파일 끝이라서 그런 거라면 정상임
	if (read_result != page_read_bytes) {
		// 파일 길이가 짧아서 그런 건지 확인
		if (read_result < page_read_bytes && read_result >= 0) {
			// debug_print(B "[INFO] Hit EOF. Read %d bytes (Expected %d). Rest zeroed.\n" W,
			// 			read_result, page_read_bytes);
		} else {
			return false; // 진짜 에러 (음수 반환 등)
		}
	}
}

/* kva 주소에 대응하는 페이지를 파일에서 읽어와 메모리에 적재한다.
	 파일 시스템과의 동시 접근 문제가 없도록 적절한 동기화가 필요 */
bool file_backed_swap_out(struct page *page)
{
	struct file_page *file_page = &page->file;

#ifdef DEBUG
	printf("\n[DEBUG] file_backed_swap_out: START\n");
#endif

	if (page->frame == NULL) {
#ifdef DEBUG
		printf("[DEBUG] file_backed_swap_out: FAIL - Page has no frame\n");
#endif
		return false;
	}

	// 더티 비트 확인해서 수정되었으면 원본에 다시 기록하기
	bool is_dirty = pml4_is_dirty(thread_current()->pml4, page->va);
	if (is_dirty) {
#ifdef DEBUG
		printf("[DEBUG]   Calling write_back()...\n");
#endif
		write_back(page);
#ifdef DEBUG
		printf("[DEBUG]   write_back() completed\n");
#endif
	}

#ifdef DEBUG
	printf("[DEBUG]   Clearing page mapping from page table...\n");
#endif

	struct frame *frame = page->frame;
	// pte에서 매핑 제거 (present bit = 0으로 만든다)
	pml4_clear_page(thread_current()->pml4, page->va);

	// 양방향 참조 제거
	page->frame = NULL;
	frame->page = NULL;

	// 물리메모리 해제
	palloc_free_page(frame->kva);
	free(frame);
	frame = NULL;

	return true;
}

// 파일 원본에 변경사항을 저장한다
static void write_back(struct page *page)
{
	struct file_page *file_page = &page->file;
	off_t result = file_write_at(file_page->file, page->frame->kva, file_page->page_read_bytes,
								 file_page->offset);
	if (result != file_page->page_read_bytes) {
		// 파일 쓰기에 실패했다면 OS가 할 수 있는 일은 없다.
		// 데이터는 유실되더라도 메모리 누수는 막아야 한다.
		debug_print("File write failed! intended: %d, actual: %d", file_page->page_read_bytes,
					result);
	}

	pml4_set_dirty(thread_current()->pml4, page->va, false);
}

/* 만약 이 페이지 내용이 수정되었다면(dirty라면),
 변경된 내용을 반드시 파일에 다시 써 줘야한다.​ */
static void file_backed_destroy(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;

	if (page->frame != NULL) {
		// 더티 비트 확인해서 수정되었으면 원본에 다시 기록하기 (Destory에서 write-back)
		bool is_dirty = pml4_is_dirty(thread_current()->pml4, page->va);
		if (is_dirty) {
			write_back(page);
		}

		// pte에서 매핑 제거
		pml4_clear_page(thread_current()->pml4, page->va);

		// 물리메모리도 해제
		palloc_free_page(page->frame->kva);
		free(page->frame);
		page->frame = NULL;
	}
}

/*
mummap을 위해 mmap시 페이지의 index와 mmap 페이지의 길이를 추가한다.
file과 aux객체에 해당 내용을 저장하여 mummap시 index와 mmap페이지 길이를 보고 mummap을 진행한다.

munmap을 위해서 struct page에 다음 페이지를 가리키는 포인터인 next_page를 추가한다.
일반적인 경우에는 next_page가 NULL이지만, mmap으로 할당할 때는 next_page가 다음 페이지를 가리키도록
업데이트한다. munmap시에는, next_page가 NULL일 때까지 순회를 돌면서 해당 munmap 로직을 수행한다.
*/
void *do_mmap(void *addr, size_t length, int writable, struct file *file, off_t offset)
{
	file = file_reopen(file);
	int index = 0;
	void *addr_copy = addr;
	size_t read_bytes = length;
	struct page *current_page = NULL;

	while (read_bytes > 0) {
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;

		struct file_page *mmap_file = malloc(sizeof(*mmap_file));
		if (!mmap_file)
			return NULL;

		*mmap_file = (struct file_page){
			.file = file,
			.offset = offset,
			.page_read_bytes = page_read_bytes,
			.index = index++,							 // mmap 중에 몇번재
			.pages_cnt = (length + PGSIZE - 1) / PGSIZE, // mmap 총 몇페이지
		};

		if (mmap_file->pages_cnt == 0) {
			debug_print(
				R
				"  [!!! WARNING !!!] pages_cnt is ZERO. length might be 0 or variable overflow.\n");
		}

		if (!vm_alloc_page_with_initializer(VM_FILE, addr_copy, writable, lazy_load_file,
											mmap_file)) {
			free(mmap_file);
			mmap_file = NULL;
			goto error;
		}

		addr_copy += PGSIZE;
		offset += page_read_bytes;
		read_bytes -= page_read_bytes;
	}

	debug_print("\n[DEBUG] do_mmap Completed!");

	return addr;

error:
	for (size_t i = 0; i < length; i += PGSIZE) {
		struct page *rollback_page = spt_find_page(&thread_current()->spt, addr + i);
		if (rollback_page != NULL)
			file_backed_destroy(rollback_page);
	}
	return NULL;
}

static bool lazy_load_file(struct page *page)
{
	struct file *file = page->file.file;
	off_t ofs = page->file.offset;
	size_t page_read_bytes = page->file.page_read_bytes;

	// 유효성 검사 (파일 포인터가 없으면 커널 패닉이나 에러가 날 수 있음)
	if (file == NULL) {
		debug_print(R "  [!!! CRITICAL !!!] File pointer is NULL. Lazy load will crash." W "\n");
		return false;
	}

	lock_acquire(&file_lock);
	int read_result = file_read_at(file, page->frame->kva, page_read_bytes, ofs);
	lock_release(&file_lock);

	if (read_result < PGSIZE) {
		page->file.page_read_bytes = read_result;
		memset(page->frame->kva + read_result, 0, PGSIZE - page_read_bytes);
	}

	// 디버깅용 : 794개 읽은 게 파일 끝이라서 그런 거라면 정상임
	if (read_result != page_read_bytes) {
		// 파일 길이가 짧아서 그런 건지 확인
		if (read_result < page_read_bytes && read_result >= 0) {
			// debug_print(B "[INFO] Hit EOF. Read %d bytes (Expected %d). Rest zeroed.\n" W,
			// 			read_result, page_read_bytes);
		} else {
			return false; // 진짜 에러 (음수 반환 등)
		}
	}

	return true;
}

/* addr라는 시작 주소부터의 특정 주소 범위에 대해 걸려 있던 매핑을 해제한다.
 이 addr는 이전에 같은 프로세스가 mmap을 호출했을 때 돌려받은 가상 주소여야 하며,
 아직 munmap으로 해제된 적이 없어야 한다.
 프로세스가 종료되면, 어떻게 종료되었든지 간에 그 프로세스가 가지고 있던 모든 매핑은 자동으로
 해제된다.​

 매핑이 해제될 때,
 그동안 프로세스가 수정한 페이지들은 파일에 다시 기록되고,
 한 번도 쓰기 되지 않은 페이지들은 파일에 기록되면 안 된다.​
 그런 다음 이 페이지들은 그 프로세스의 가상 페이지 목록에서 제거된다.

 어떤 파일을 닫거나 삭제하더라도,
 그 파일에 대해 만들어진 메모리 매핑이 자동으로 해제되지는 않는다.​
 한 번 만들어진 매핑은 munmap을 호출하거나 프로세스가 종료될 때까지
 유효하다.​ 더 자세한 내용은 “Removing an Open File” 부분을 참고.

 둘 이상의 프로세스가 같은 파일을 매핑하더라도,
 서로가 항상 똑같은 (완전히 일관된) 데이터를 본다는 보장은 없다.​*/
void do_munmap(void *addr)
{
	// 1. 해제할 범위가 어디까지인가? : start_page부터 PGSIZE * pages_cnt만큼
	struct page *start_page = spt_find_page(&(thread_current()->spt), addr);
	if (!start_page) {
		debug_print(R "[DEBUG]: FAIL - Start page not found for addr %p" W "\n", addr);
		return; // void 함수이므로 return NULL은 경고 발생 가능
	}

	uint32_t pages_cnt = start_page->file.pages_cnt;
	struct file *file = start_page->file.file; // 파일 포인터를 미리 저장 use-after-free

	// 2. 해당 범위에 해제할 페이지가 있는가?
	for (uint32_t i = 0; i < pages_cnt; i++) {
		void *curr_addr = addr + (PGSIZE * i);
		struct page *curr_page = spt_find_page(&(thread_current()->spt), curr_addr);

		// spt에 없으면 해제 불가
		if (NULL == curr_page) {
			debug_print(R "[DEBUG]: Page not found at %p. Stop unmapping." W "\n", curr_addr);
			return;
		}

		// spt에 있으면 해제
		// spt_remove_page : hash 삭제 > destory() > pml4_clear_page > 물리메모리 해제
		spt_remove_page(&(thread_current()->spt), curr_page);
	}

	// 파일 닫기
	file_close(file);
	file = NULL;

	debug_print(M "[DEBUG] do_munmap finished successfully.\n" W);
}
