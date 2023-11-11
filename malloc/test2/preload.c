#include <stdio.h>

extern void kernel_heap_init();

void __attribute__((constructor)) run_me_first() {
	printf("kernel_heap_init()\n");
	kernel_heap_init();
}
	
void *malloc(size_t size) {
	return __wrap_malloc(size);
}

void free(void *ptr) {
	return __wrap_free(ptr);
}

void *realloc(void *ptr, size_t size) {
	return __wrap_realloc(ptr, size);
}

