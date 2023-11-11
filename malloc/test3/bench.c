#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include "allocator.h"

#include <signal.h>
#include <libunwind.h>

//#define MEM_SIZE (4096 * 30)
#define MEM_SIZE (1024 * 1024 * 128)
//#define MEM_SIZE (1024 * 1024 * 8)
#define NUM_THREADS 16
#define RUNS 5

#define TOTAL_PAGES (MEM_SIZE / 4096)
#define MAX_ALLOCATIONS_PER_THREAD ((TOTAL_PAGES - 20)/ NUM_THREADS)

#define REALLOCATION_ITERATIONS 1000

#ifdef SYSTEM_MALLOC
static inline void* allocate_page() {
    return malloc(PAGE_SIZE);
}
static inline void free_page(void* ptr) {
    free(ptr);
}
static inline void* allocate_page_link(size_t num) {
    return malloc(PAGE_SIZE * num);
}
static inline void free_page_link(void* ptr) {
    free(ptr);
}
static inline void* allocate_slab(size_t size) {
    return malloc(size);
}
static inline void free_slab(void* ptr) {
    free(ptr);
}
#else
static inline void* allocate_page() {
    return page_alloc(ALLOCATOR_PAGE, 0);
}
static inline void free_page(void* ptr) {
    page_free(ptr);
}
static inline void* allocate_page_link(size_t num) {
    return page_alloc_link(num);
}
static inline void free_page_link(void* ptr) {
    page_free_link(ptr);
}
static inline void* allocate_slab(size_t size) {
    return slab_alloc(size);
}
static inline void free_slab(void* ptr) {
    slab_free(ptr);
}
#endif

static char* executable;

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
}

void signal_handler(int sig, siginfo_t *si, void *unused) {
    (void)si;  // Suppress unused parameter warning.
    (void)unused;  // Suppress unused parameter warning.

    fflush(stdout);
    printf("Caught signal %d (%s)\n", sig, strsignal(sig));
    print_backtrace();

    exit(sig);
}

atomic_size_t highest_pointer = 0;
atomic_size_t lowest_pointer = UINT64_MAX;

static inline void update_pointer(void* ptr) {
    if ((size_t)ptr > atomic_load(&highest_pointer)) {
	atomic_store(&highest_pointer, (size_t)ptr);
    }
    if ((size_t)ptr < atomic_load(&lowest_pointer)) {
	atomic_store(&lowest_pointer, (size_t)ptr);
    }
}

void* thread_work(void* arg) {
    char** allocated_pages = malloc(MAX_ALLOCATIONS_PER_THREAD * sizeof(void*));
    memset(allocated_pages, 0, MAX_ALLOCATIONS_PER_THREAD * sizeof(void*));

    for (int i = 0; i < MAX_ALLOCATIONS_PER_THREAD; ++i) {
        char* page = allocate_page();
        if (!page) {
		printf("Allocation failed: %i, %zi, %zi\n", i, get_pages(), get_free_pages());
                print_size_skiplist();
		exit(1);
	}
	update_pointer(page);
        allocated_pages[i] = page;
    }

    for (int i = 0; i < REALLOCATION_ITERATIONS; ++i) {
        int index = random() % (MAX_ALLOCATIONS_PER_THREAD - 1);
        if (allocated_pages[index]) {
            free_page(allocated_pages[index]);
	    char* page = allocate_page();
            allocated_pages[index] = page;

	    update_pointer(page);

	    if (!allocated_pages[index]) {
	    	printf("Reallocation failed : %zi, %zi\n", get_pages(), get_free_pages());
		exit(1);
	    }
        }
    }

    for (int i = 0; i < MAX_ALLOCATIONS_PER_THREAD; ++i) {
        free_page(allocated_pages[i]);
	allocated_pages[i] = NULL;
    }

    uint32_t allocated = 0;
    uint32_t idx = 2;
    uint32_t alloc = MAX_ALLOCATIONS_PER_THREAD / 2;
    while (allocated + alloc < MAX_ALLOCATIONS_PER_THREAD) {
        char* page = allocate_page_link(alloc);
        if (!page) {
            //printf("Link allocation failed : %i, %zi, %zi\n", alloc, get_pages(), get_free_pages());
        } else {
            allocated_pages[idx] = page;
        }

        allocated += alloc;
        ++idx;
        alloc = MAX_ALLOCATIONS_PER_THREAD / idx;
    }

    for (uint32_t i = 2; i <= idx; ++i) {
        free_page_link(allocated_pages[i]);
	allocated_pages[i] = NULL;
    }

    allocated = 0;
    idx = 2;
    while (allocated + idx < MAX_ALLOCATIONS_PER_THREAD) {
	    char* page = allocate_page_link(idx);
	    if (!page) {
	    	//printf("Link allocation failed : %i, %zi, %zi\n", idx + 2, get_pages(), get_free_pages());
	    } else {
		allocated_pages[idx] = page;
		update_pointer(page);
	    }

	    allocated += idx;
	    ++idx;
    }

    for (uint32_t i = 2; i <= idx; ++i) {
        free_page_link(allocated_pages[i]);
	allocated_pages[i] = NULL;
    }

    size_t max_slab_allocations = ((MAX_ALLOCATIONS_PER_THREAD * 4096) / 32) * sizeof(void*);
    void** allocations = malloc(max_slab_allocations);
    size_t bytes_allocated = 0;
    memset(allocations, 0, max_slab_allocations);
    size_t slab_allocations_done = 0;

    uint16_t slab_size = slab_sizes[(random() % 4)];
    while (bytes_allocated + slab_size < MAX_ALLOCATIONS_PER_THREAD * (4096 - 256)) {
        //uint16_t slab_size = slab_sizes[3];
        allocations[slab_allocations_done] = allocate_slab(slab_size);
        if (!allocations[slab_allocations_done]) {
                printf("Slab allocation of size %i failed: total pages: %zi, free pages:%zi, allocated bytes: %zi\n", slab_size, get_pages(), get_free_pages(), bytes_allocated);
                exit(1);
        }
        bytes_allocated += slab_size;
	slab_size = slab_sizes[(random() % 4)];
        ++slab_allocations_done;
    }

#if 0
    void* allocations[MAX_ALLOCATIONS_PER_THREAD * 4];
    memset(allocations, 0, sizeof(allocations));
    for (uint32_t i = 0; i < MAX_ALLOCATIONS_PER_THREAD * 4; ++i) {
	//uint16_t slab_size = slab_sizes[3];
	uint16_t slab_size = slab_sizes[abs(random() % 4)];
	void* alloc = allocate_slab(slab_size);
	allocations[i] = alloc;
	if (!allocations[i]) {
		printf("Slab allocation of size %i failed: %i, %zi, %zi\n", slab_size, i, get_pages(), get_free_pages());
		exit(1);
	}
	update_pointer(alloc);
    }
#endif

    for (int i = 0; i < REALLOCATION_ITERATIONS; ++i) {
        int index = random() % slab_allocations_done;
        if (allocations[index]) {
            free_slab(allocations[index]);
	    uint16_t slab_size = slab_sizes[random() % 4];
	    char* slab = allocate_slab(slab_size);
            allocations[index] = slab;

	    if (!allocations[index]) {
		printf("Slab reallocation of size %i failed: %i, %zi, %zi\n", slab_size, i, get_pages(), get_free_pages());
		exit(1);
	    }

	    update_pointer(slab);
        }
    }

#if 0
    for (uint32_t i = 0; i < MAX_ALLOCATIONS_PER_THREAD * 4; ++i) {
	void* compare = allocations[i];

    	for (uint32_t k = 0; k < MAX_ALLOCATIONS_PER_THREAD * 4; ++k) {
		if (i == k) continue;

		if (compare == allocations[k]) {
			printf("Duplicate allocation %p\n", compare);
			exit(1);
		}
	}
   }
#endif

    for (uint32_t i = 0; i < slab_allocations_done; ++i) {
	free_slab(allocations[i]);
    }

    free(allocations);
    //printf("Thread finished, %li bytes in slab allocations\n", bytes_allocated);

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

    srandom(time(NULL));
    pthread_t threads[NUM_THREADS];
    int thread_nums[NUM_THREADS];

#ifndef SYSTEM_MALLOC
    void* start = malloc(MEM_SIZE);
    void* end = (char*)start + MEM_SIZE;
    page_alloc_init(start, end);
    atomic_store(&lowest_pointer, (size_t)start);
#endif

    for (int runs = 0; runs < RUNS; ++runs) {
	for (int i = 0; i < NUM_THREADS; ++i) {
	    thread_nums[i] = i;
	    pthread_create(&threads[i], NULL, thread_work, &thread_nums[i]);
	}

	for (int i = 0; i < NUM_THREADS; ++i) {
	    pthread_join(threads[i], NULL);
	}
	//printf("Run: %i complete\n", runs);
	printf(".");
	fflush(stdout);
    }
    printf("\n");

#ifndef SYSTEM_MALLOC
    free(start);
#endif

    printf("Lowest pointer: 0x%16lX, Highest pointer: 0x%16lX, difference: %zi\n", lowest_pointer, highest_pointer, highest_pointer - lowest_pointer);
    return 0;
}

