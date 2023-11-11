#include <stdio.h>
#include <stdlib.h>

#include "wrap.h"
#include "syscalls.h"

void * __wrap_malloc(size_t c) {
    void *addr = __builtin_extract_return_addr(__builtin_return_address(0));
    printf ("malloc called with %zu from %p pid %i\n", c, addr, find_pid(addr));
    return __real_malloc(c);
}

void __wrap_free(void *ptr) {
    void *addr = __builtin_extract_return_addr(__builtin_return_address(0));
    printf ("free called with %p from %p pid %i\n", ptr, addr, find_pid(addr));
    return __real_free(ptr);
}
