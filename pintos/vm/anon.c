/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/mmu.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void vm_anon_init(void)
{
	/* TODO: Set up the swap_disk. */
	swap_disk = NULL;
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	// 익명 페이지 초기화 (현재는 추가 필드 없음)

	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool anon_swap_in(struct page *page, void *kva)
{
	struct anon_page *anon_page = &page->anon;

	// TODO: 스왑 디스크에서 데이터 읽어오기 (Project 3 Extra)
	// 현재는 새로운 익명 페이지만 지원
	// kva는 이미 0으로 초기화됨 (PAL_ZERO)

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool anon_swap_out(struct page *page)
{
	struct anon_page *anon_page = &page->anon;

	// TODO: 스왑 디스크에 데이터 쓰기
	// 1. 스왑 슬롯 할당
	// 2. disk_write(swap_disk, anon_page->swap_slot, page->frame->kva);
	// 3. 프레임 해제

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void anon_destroy(struct page *page)
{
	struct anon_page *anon_page = &page->anon;

	// frame이 있으면 자원 해제
	if (NULL != page->frame) {
		// pte에서 매핑 제거
		pml4_clear_page(thread_current()->pml4, page->va);

		// 물리메모리해제
		palloc_free_page(page->frame->kva);

		// frame 구조체해제
		free(page->frame);
		page->frame = NULL;
	}
}
