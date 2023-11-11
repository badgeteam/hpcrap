#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#include <signal.h>
#include <libunwind.h>

#include "allocator.h"

//#define MEM_SIZE (4096 * 30)
//#define MEM_SIZE (1024 * 1024 * 128)
#define MEM_SIZE (1024 * 1024 * 32)
//#define MEM_SIZE (1024 * 1024 * 10)

#define TOTAL_PAGES (MEM_SIZE / 4096)
#define NUM_THREADS 32
#define MAX_ALLOCATIONS_PER_THREAD ((TOTAL_PAGES - 10)/ NUM_THREADS)

#define REALLOCATION_ITERATIONS 1000

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

void* thread_work(void* arg) {
    void* allocated_pages[MAX_ALLOCATIONS_PER_THREAD];
    memset(allocated_pages, 0, sizeof(allocated_pages));
    int thread_num = *((int*) arg);
    if (!silent) printf("Thread %d started\n", thread_num);

    for (size_t i = 0; i < MAX_ALLOCATIONS_PER_THREAD; ++i) {
        char* page = page_alloc(ALLOCATOR_PAGE, 0);
        if (!page) {
		printf("Thread: %d: Allocation failed: %zi, %zi, %zi\n", thread_num, i, get_pages(), get_free_pages());
                print_size_skiplist();
		//exit(1);
                continue;
	}
        allocated_pages[i] = page;
	delay_rand();
    }

    for (size_t i = 0; i < REALLOCATION_ITERATIONS; ++i) {
        int index = rand() % MAX_ALLOCATIONS_PER_THREAD;
        if (allocated_pages[index]) {
            page_free(allocated_pages[index]);
            allocated_pages[index] = page_alloc(ALLOCATOR_PAGE, 0);
	    if (!allocated_pages[index]) {
	    	printf("Reallocation failed : %zi, %zi\n", get_pages(), get_free_pages());
		//exit(1);
                allocated_pages[index] = NULL;
                continue;
	    }
        }
	delay_rand();
    }

    printf("Thread: %d: (Page) max size: %zi, total available: %zi\n", thread_num, get_largest_size(), get_free_pages());
    for (size_t i = 0; i < MAX_ALLOCATIONS_PER_THREAD; ++i) {
        page_free(allocated_pages[i]);
	delay_rand();
    }

    memset(allocated_pages, 0, sizeof(allocated_pages));

    uint32_t allocated = 0;
    uint32_t idx = 2;
    uint32_t alloc = MAX_ALLOCATIONS_PER_THREAD / 4;
    while (allocated + alloc < MAX_ALLOCATIONS_PER_THREAD) {
        printf("Thread: %d, allocating link of size %zi\n", thread_num, alloc);
        char* page = page_alloc_link(alloc);
        if (!page) {
            printf("Thread: %d, Link allocation failed (half) : size: %i, pages: %zi, free_pages: %zi, largest_size: %zi\n", thread_num, alloc, get_pages(), get_free_pages(), get_largest_size());
            print_size_skiplist();
            //exit(1);
            break;
        } else {
            allocated_pages[idx] = page;
            allocated += alloc;
        }

        ++idx;
	alloc = MAX_ALLOCATIONS_PER_THREAD / idx;
	delay_rand();
    }

    for (uint32_t i = 0; i < MAX_ALLOCATIONS_PER_THREAD; ++i) {
        page_free_link(allocated_pages[i]);
	delay_rand();
    }

    printf("Thread: %d, large link succeeded\n", thread_num);
    memset(allocated_pages, 0, sizeof(allocated_pages));

    allocated = 0;
    idx = 2;
    while (allocated + idx < MAX_ALLOCATIONS_PER_THREAD) {
        printf("Thread: %d, allocating link of size %zi\n", thread_num, alloc);
        char* page = page_alloc_link(idx);
        if (!page) {
            printf("Thread: %d, Link allocation failed (rand) : size: %i, pages: %zi, free_pages: %zi, largest_size: %zi\n", thread_num, alloc, get_pages(), get_free_pages(), get_largest_size());
            print_size_skiplist();
            //exit(1);
            break;
        } else {
            allocated_pages[idx] = page;
            allocated += idx;
        }

        ++idx;
	delay_rand();
    }

    for (uint32_t i = 0; i < MAX_ALLOCATIONS_PER_THREAD; ++i) {
        page_free_link(allocated_pages[i]);
	delay_rand();
    }
    printf("Thread: %d, small link succeeded\n", thread_num);

    memset(allocated_pages, 0, sizeof(allocated_pages));

    void* allocations[(MAX_ALLOCATIONS_PER_THREAD * 4096 - 256) / 32];
    size_t bytes_allocated = 0;
    memset(allocations, 0, sizeof(allocations));
    size_t i = 0;

    uint16_t slab_size = slab_sizes[abs(rand() % 4)];
    while (bytes_allocated + slab_size < MAX_ALLOCATIONS_PER_THREAD * (4096 - 256)) {
	//uint16_t slab_size = slab_sizes[3];
	allocations[i] = slab_alloc(slab_size);
	if (!allocations[i]) {
		printf("Slab allocation of size %i failed: total pages: %zi, free pages:%zi, allocated bytes: %zi\n", slab_size, get_pages(), get_free_pages(), bytes_allocated);
		//exit(1);
                break;
	}
        bytes_allocated += slab_size;
	slab_size = slab_sizes[abs(rand() % 4)];
        ++i;
	delay_rand();
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

    printf("Thread: %d: (Slab) max size: %zi, total available: %zi\n", thread_num, get_largest_size(), get_free_pages());
    for (uint32_t k = 0; k < i; ++k) {
	slab_free(allocations[k]);
	delay_rand();
    }

    if (!silent) printf("Thread %d finished, %li bytes in slab allocations\n", thread_num, bytes_allocated);
    return NULL;
}

#define SKIP_LIST_MAX_LEVEL 4

typedef struct slab_header {
    enum slab_sizes       size;
    atomic_uintptr_t      next[SKIP_LIST_MAX_LEVEL];
    atomic_uint_least32_t bitmap[4];
    atomic_uint_least32_t use_count;
    atomic_uint_least32_t status;
} slab_header_t;

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
    int thread_nums[NUM_THREADS];

    void* start = calloc(MEM_SIZE, 1);
    void* end = (char*)start + MEM_SIZE;
    if (!start) {
        printf("Initial (real) malloc failed\n");
        exit(1);
    }

    page_alloc_init(start, end);

    if (argc == 2) silent = true;

    do {
    for (int i = 0; i < NUM_THREADS; ++i) {
        thread_nums[i] = i;
        pthread_create(&threads[i], NULL, thread_work, &thread_nums[i]);
    }

    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_join(threads[i], NULL);
    }

    if (silent) {
	printf(".");
	fflush(stdout);
    }

    deallocate_inactive();
    quickpool_destroy(1);

    uint32_t tries = 10000;
    while (tries && get_free_pages() != get_pages()) {
	    --tries;
    }

    if (get_free_pages() != get_pages()) {
        printf("Error: Free pages count does not match! Expected %zu, but got %zu, missing %zu (After threads)\n", get_pages(), get_free_pages(), get_pages() - get_free_pages());
        print_size_skiplist();
	for (uint32_t i = 0; i < get_pages(); ++i) {
	    void* page = get_page_by_index(i);
	    if (!is_page_free(page)) {
		uint8_t type = get_page_type(i);
		switch(type) {
		    case ALLOCATOR_PAGE: {
		    	printf("page: %p not free, type: PAGE\n", page);
		    	break;
	    	    }
		    case ALLOCATOR_SLAB: {
		        slab_header_t* header = (slab_header_t*)page;
		        printf("page: %p not free, type: SLAB, use_count: %08X, status: %08X, next[0]: %p\n", page, atomic_load(&header->use_count), atomic_load(&header->status), (void*)atomic_load(&header->next[0]));
		        for (uint32_t k = 0; k < 4; ++k) {
		    	    printf("%08X ", atomic_load(&header->bitmap[i]));
		        }
		        printf("\n");
		        break;
		    }
		    case ALLOCATOR_BUDDY: {
		    	printf("page: %p not free, type: BUDDY\n", page);
		    	break;
		    }
		    case ALLOCATOR_PAGE_LINK: {
		    	printf("page: %p not free, type: PAGE_LINK\n", page);
		    	break;
		    }
		    default: {
		    	printf("page: %p not free, type: UNKNOWN\n", page);
			break;
		    }
		}
	    }
	}
        return 1;
        //return 0;
    }
    } while(argc == 2);

    char* main_allocations[get_pages()];
    memset(main_allocations, 0, sizeof(main_allocations));

    for (size_t i = 0; i < get_pages(); ++i) {
        char* page = page_alloc(ALLOCATOR_PAGE, 0);
	if(!page) {
	    printf("Allocation failed: %zi, %zi, %zi\n", i, get_pages(), get_free_pages());
            exit(1);
        } else {
            main_allocations[i] = page;
        }
    }

    char* page = page_alloc(ALLOCATOR_PAGE, 0);
    if (page) {
	    printf("Allocation succeeded when it should have failed\n");
	    exit(1);
    }

    #define TEST 10
    uint8_t *allocations[TEST];
    for (int i = 0; i < TEST; ++i) {
	    uint8_t* alloc = slab_alloc(140);
	    if (alloc) {
	    	printf("Slab allocation succeeded when it should have failed\n");
	    	exit(1);
	    }
    }

    for (size_t i = 0; i < get_pages(); ++i) {
        page_free(main_allocations[i]);
    }
    memset(main_allocations, 0, sizeof(main_allocations));

    for (int i = 0; i < TEST; ++i) {
	    uint8_t* alloc = slab_alloc(140);
	    if(!alloc) {
		    printf("Slab allocation failed with it should have succeeded\n");
		    exit(1);
	    }
	    allocations[i] = alloc;
    }

    for (int i = TEST - 1; i >= 0 ; --i) {
	    slab_free(allocations[i]);
    }

    printf("Allocate link1: %zi\n", get_pages() / 4U - 2);
    void* link1 = page_alloc_link(get_pages() / 4U - 2);
    if (!link1) {
	    printf("Link1 allocation failed when it should have succeeded\n");
            print_size_skiplist();
	    exit(1);
    }
    printf("Allocate link2: %zi\n", get_pages() / 2U - 2);
    void* link2 = page_alloc_link(get_pages() / 2U - 2);
    if (!link2) {
	    printf("Link2 allocation failed when it should have succeeded\n");
            print_size_skiplist();
	    exit(1);
    }
    printf("Allocate link3: %zi\n", get_pages() / 4U - 2);
    void* link3 = page_alloc_link(get_pages() / 4U - 2);
    if (!link3) {
	    printf("Link3 allocation failed when it should have succeeded\n");
            print_size_skiplist();
	    exit(1);
    }

    printf("link1: %p, link2: %p, link3: %p\n", link1, link2, link3);
    printf("Free link1:\n");
    page_free_link(link1);
    printf("Free link2:\n");
    page_free_link(link2);
    printf("Free link3:\n");
    page_free_link(link3);

    deallocate_inactive();
    quickpool_destroy(0);

    if (get_free_pages() != get_pages()) {
        printf("Error: Free pages count does not match! Expected %zu, but got %zu (at end)\n", get_pages(), get_free_pages());
    	exit(1);
    }

    if (get_free_pages() != get_largest_size()) {
        printf("Error: Free pages count does not match largest size expected %zu, got %zu\n", get_pages(), get_largest_size());
        exit(1);
    }

    printf("All tests passed.\n");

    free(start);
    return 0;
}

