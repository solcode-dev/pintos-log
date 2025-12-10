/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/vaddr.h"
#include <bitmap.h>

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

static struct bitmap *swap_table;

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
	swap_disk = disk_get(1, 1);
	swap_table = bitmap_create(disk_size(swap_disk) / (PGSIZE / DISK_SECTOR_SIZE));
	if (swap_table == NULL)
		// PANIC("vm_anon_init: cannot create swap bitmap");
		printf("vm_anon_init: cannot create swap bitmap");

	bitmap_set_all(swap_table, false);
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	anon_page->swap_table_index = BITMAP_ERROR;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool anon_swap_in(struct page *page, void *kva)
{
	struct anon_page *anon_page = &page->anon;
	size_t bitmap_index = anon_page->swap_table_index;

	if (bitmap_index == BITMAP_ERROR)
		return false;

	disk_sector_t start_disk_sec = bitmap_index * 8;
	for (int i = 0; i < (PGSIZE / DISK_SECTOR_SIZE); i++) {
		disk_read(swap_disk, (start_disk_sec + i), kva + (DISK_SECTOR_SIZE * i));
	}

	bitmap_set(swap_table, bitmap_index, false);
	anon_page->swap_table_index = BITMAP_ERROR;
	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool anon_swap_out(struct page *page)
{
	struct anon_page *anon_page = &page->anon;

	if (anon_page->swap_table_index != BITMAP_ERROR)
		return false;

	size_t bitmap_index = bitmap_scan_and_flip(swap_table, 0, 1, false);
	if (bitmap_index == BITMAP_ERROR)
		return false;

	disk_sector_t start_disk_sec = bitmap_index * 8;
	for (int i = 0; i < (PGSIZE / DISK_SECTOR_SIZE); i++) {
		disk_write(swap_disk, (start_disk_sec + i), page->frame->kva + (DISK_SECTOR_SIZE * i));
	}

	anon_page->swap_table_index = bitmap_index;
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void anon_destroy(struct page *page)
{
	struct anon_page *anon_page = &page->anon;

	// swap disk 있으면 해제
	if (anon_page->swap_table_index != BITMAP_ERROR) {
		bitmap_set(swap_table, anon_page->swap_table_index, false);
		anon_page->swap_table_index = BITMAP_ERROR;
	}

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
