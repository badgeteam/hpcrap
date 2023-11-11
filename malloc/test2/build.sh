gcc -std=gnu17 -g3 -Wall -Wextra main.c malloc.c -Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,calloc -Wl,--wrap,realloc -Wl,--wrap,reallocarray -Wl,--wrap,aligned_alloc -Wl,--wrap,posix_memalign -o malloc
gcc -std=gnu17 -g3 -Wall -Wextra main.c malloc.c -Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,calloc -Wl,--wrap,realloc -Wl,--wrap,reallocarray -Wl,--wrap,aligned_alloc -Wl,--wrap,posix_memalign -DBADGEROS_MALLOC_DEBUG_LEVEL=4 -o malloc-debug
#gcc -std=gnu17 -g3 -Wall -Wextra test.c malloc.c -Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,calloc -Wl,--wrap,realloc -Wl,--wrap,reallocarray -o malloc-test
gcc -std=gnu17 -g3 -Wall -Wextra -DPRELOAD malloc.c -Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,calloc -Wl,--wrap,realloc -Wl,--wrap,reallocarray -Wl,--wrap,aligned_alloc -Wl,--wrap,posix_memalign -fpic -shared -o malloc.so
gcc -std=gnu17 -g3 -Wall -Wextra -DPRELOAD malloc.c -Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,calloc -Wl,--wrap,realloc -Wl,--wrap,reallocarray -Wl,--wrap,aligned_alloc -Wl,--wrap,posix_memalign -fpic -shared -DBADGEROS_MALLOC_DEBUG_LEVEL=4 -o malloc-debug.so

