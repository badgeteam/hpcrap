gcc -std=gnu17 -g3 -Wall -Wextra main.c malloc.c pid_sim.c -Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,calloc -Wl,--wrap,realloc -Wl,--wrap,reallocarray -o malloc
clang -std=gnu17 -g3 -Wall -Wextra main.c malloc.c pid_sim.c -Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,calloc -Wl,--wrap,realloc -Wl,--wrap,reallocarray -o malloc-clang
