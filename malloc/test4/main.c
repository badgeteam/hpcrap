#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#include <signal.h>
#include <libunwind.h>

#include "allocator.h"
#include "alloc-page.h"

//#define MEM_SIZE (4096 * 30)
#define MEM_SIZE (1024 * 1024 * 128)
//#define MEM_SIZE (1024 * 1024 * 32)
//#define MEM_SIZE (1024 * 1024 * 10)

#define NUM_THREADS 8
#define TOTAL_PAGES (32206 - NUM_THREADS)
#define MAX_ALLOCATIONS_PER_THREAD (TOTAL_PAGES / NUM_THREADS)

extern page_pool_t *kernel_pool;

bool silent = false;

char* executable;

void print_backtrace() {
    unw_cursor_t cursor;
    unw_context_t context;

    // Initialize cursor to current frame for local unwinding.
    unw_getcontext(&context);
    unw_init_local(&cursor, &context);

    // Unwind frames one by one, going up the frame stack.
    while (unw_step(&cursor) > 0) {
        unw_word_t offset, pc;
        char fname[64] = {0};
        char command[256] = {0};
        char line[256] = {0};

        unw_get_reg(&cursor, UNW_REG_IP, &pc);
        if (pc == 0) {
            break;
        }
        unw_get_proc_name(&cursor, fname, sizeof(fname), &offset);

        // Use addr2line to get file and line information
        sprintf(command, "addr2line -e %s -C %lx", executable, pc);
        FILE *fp = popen(command, "r");
        if (fp) {
            char *fileline = fgets(line, sizeof(line), fp);
            if (fileline) {
                // Remove newline
                size_t len = strlen(fileline);
                if (len > 0 && fileline[len - 1] == '\n') {
                    fileline[len - 1] = '\0';
                }
            }
            pclose(fp);
        }

        printf("%s at %s\n", fname, line);
    }
    print_skiplist(&kernel_pool->pages_list);
}

void signal_handler(int sig, siginfo_t *si, void *unused) {
    (void)si;  // Suppress unused parameter warning.
    (void)unused;  // Suppress unused parameter warning.

    fflush(stdout);
    printf("Caught signal %d (%s)\n", sig, strsignal(sig));
    print_backtrace();

    exit(sig);
}

static inline void delay_rand() {
      //if (rand() % 10 == 0) {  // Random delay
      //    usleep(rand() % 1000);
      //}
}

typedef struct {
    int thread_num;
    int offset;
    int alloc;
} thread_params;

void* thread_work(void* a) {
    thread_params* arg = a;
    int allocated = 0;
    int size = (rand() % 4) + 1;
    int index = arg->offset;

    printf("Started thread %i at index %i\n", arg->thread_num, index);

    while (true) {
        //printf("Thread %i: inserting at index %i, size %i\n", thread_num, index, size);
        kernel_page_alloc_free(index, size);
        index += size;
        allocated += size;
        size = (rand() % 4) + 1;

        if (allocated == arg->alloc) {
            break;
        }

        if (allocated + size > arg->alloc) {
            size = arg->alloc - allocated;
        }
    }
    
    printf("Stopped thread %i inserted %i\n", arg->thread_num, allocated);
    return NULL;
}

int main(int argc, char* argv[]) {
    executable = argv[0];

    struct sigaction sa;

    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;
    sigemptyset(&sa.sa_mask);

    // Catch multiple signals. Add any other signals you want to handle here.
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    srand(time(NULL));
    pthread_t threads[NUM_THREADS];
    thread_params t[NUM_THREADS];

    void* start = calloc(MEM_SIZE, 1);
    void* end = (char*)start + MEM_SIZE;
    if (!start) {
        printf("Initial (real) malloc failed\n");
        exit(1);
    }

    size_t t_off = 0;
    for (int i = 0; i < NUM_THREADS; ++i) {
        t[i].thread_num = i;
        t[i].offset = t_off;
        t[i].alloc = MAX_ALLOCATIONS_PER_THREAD - i;
        t_off += MAX_ALLOCATIONS_PER_THREAD + 1;
    }

    page_alloc_init(start, end);

    if (argc == 2) silent = true;

    do {
        for (int i = 0; i < NUM_THREADS; ++i) {
            pthread_create(&threads[i], NULL, thread_work, &t[i]);
        }

        for (int i = 0; i < NUM_THREADS; ++i) {
            pthread_join(threads[i], NULL);
        }

        if (silent) {
            printf(".");
            fflush(stdout);
        }
    } while(argc == 2);


    //kernel_page_alloc_free(start + (432 * 4096));
    print_skiplist(&kernel_pool->pages_list);
    printf("All tests passed.\n");
    free(start);
    return 0;
}

