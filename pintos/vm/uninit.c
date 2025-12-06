/* uninit.c: Implementation of uninitialized page.
 *
 * All of the pages are born as uninit page. When the first page fault occurs,
 * the handler chain calls uninit_initialize (page->operations.swap_in).
 * The uninit_initialize function transmutes the page into the specific page
 * object (anon, file, page_cache), by initializing the page object,and calls
 * initialization callback that passed from vm_alloc_page_with_initializer
 * function.
 * */

#include "vm/vm.h"
#include "vm/uninit.h"

static bool uninit_initialize(struct page *page, void *kva);
static void uninit_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations uninit_ops = {
	.swap_in = uninit_initialize,
	.swap_out = NULL,
	.destroy = uninit_destroy,
	.type = VM_UNINIT,
};

/* DO NOT MODIFY this function */
void uninit_new(struct page *page, void *va, vm_initializer *init, enum vm_type type, void *aux,
				bool (*initializer)(struct page *, enum vm_type, void *))
{
	ASSERT(page != NULL);

	*page = (struct page){.operations = &uninit_ops,
						  .va = va,
						  .frame = NULL, /* no frame for now */
						  .uninit = (struct uninit_page){
							  .init = init,
							  .type = type,
							  .aux = aux,
							  .page_initializer = initializer,
						  }};
}

/* Initalize the page on first fault */
static bool uninit_initialize(struct page *page, void *kva)
{
	struct uninit_page *uninit = &page->uninit;

	/* Fetch first, page_initialize may overwrite the values */
	vm_initializer *init = uninit->init;
	void *aux = uninit->aux;

	/* TODO: You may need to fix this function. */
	return uninit->page_initializer(page, uninit->type, kva) && (init ? init(page, aux) : true);
}

/* 프로세스 종료 시점까지 한 번도 참조되지 않은 uninit 페이지가 보유한 자원을
정리한다.​ 대부분의 페이지는 다른 페이지 객체로 전환되지만,
예외적으로 실행 동안 사용되지 않은
uninit 페이지가 남을 수 있다.​ PAGE 구조체 자체의 해제는 이 함수를 호출한
쪽에서 수행한다.*/
static void uninit_destroy(struct page *page)
{
	struct uninit_page *uninit UNUSED = &(page->uninit);
	// uninit 주소는 항상으므로 유효성 체크 안함

	// 파일 페이지가 아니라면 패스
	if (VM_TYPE(uninit->type) != VM_FILE) {
		return;
	}

	// TODO: mmap 구현에 따라 아래 부분 달라짐
	//  aux가 없다면 패스
	if (NULL == uninit->aux) {
		return;
	}

	// 데이터 삭제
	free(uninit->aux);
	// 혹시 모를 초기화. 버릇처럼! - 영근가라사대
	uninit->aux = NULL;
}
