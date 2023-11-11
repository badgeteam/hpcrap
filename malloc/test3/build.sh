gcc -Wall -Wextra -std=gnu17 -DBITMAP_WORD_BITS=32 -g3 -Wall -Wextra main.c malloc.c alloc-*.c -lunwind -lunwind-x86_64 -Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,calloc -Wl,--wrap,realloc -Wl,--wrap,reallocarray -o malloc32
gcc -Wall -Wextra -std=gnu17 -DBITMAP_WORD_BITS=64 -g3 -Wall -Wextra main.c malloc.c alloc-*.c -lunwind -lunwind-x86_64 -Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,calloc -Wl,--wrap,realloc -Wl,--wrap,reallocarray -o malloc64
gcc -Wall -Wextra -std=gnu17 -DBITMAP_WORD_BITS=32 -DSOFTBIT -g3 -Wall -Wextra main.c malloc.c alloc-*.c -lunwind -lunwind-x86_64 -Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,calloc -Wl,--wrap,realloc -Wl,--wrap,reallocarray -o malloc32-softbit
gcc -Wall -Wextra -std=gnu17 -DBITMAP_WORD_BITS=64 -DSOFTBIT -g3 -Wall -Wextra main.c malloc.c alloc-*.c -lunwind -lunwind-x86_64 -Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,calloc -Wl,--wrap,realloc -Wl,--wrap,reallocarray -o malloc64-softbit

