#include <stddef.h>

void *__real_malloc(size_t size);
void __real_free(void *ptr);
void *__real_calloc(size_t nmemb, size_t size);
void *__real_realloc(void *ptr, size_t size);
void *__real_reallocarray(void *ptr, size_t nmemb, size_t size);

void *__wrap_malloc(size_t size) {
        return __real_malloc(size);
}

void __wrap_free(void *ptr) {
        return __real_free(ptr);
}

void *__wrap_calloc(size_t nmemb, size_t size) {
        return __real_calloc(nmemb, size);
}

void *__wrap_realloc(void *ptr, size_t size) {
        return __real_realloc(ptr, size);
}

void *__wrap_reallocarray(void *ptr, size_t nmemb, size_t size) {
        return __real_reallocarray(ptr, nmemb, size);
}

