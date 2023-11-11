#include <stdatomic.h>
#include <stdbool.h>

#ifndef BADGEROS_KERNEL
#include <stdio.h>
#include <stdlib.h>
#endif

#include "allocator.h"
#include "quickpool.h"

#define SKIPLIST_MAX_LEVEL 10
#define RANGE_MAGIC 0XC0DECAFE

// These are kind of large, but they exist in free memory
typedef struct {
    atomic_uint_least32_t magic; // Used as a flag to prevent double use
    atomic_uint_least32_t level; // The current level of the node
    atomic_size_t start;         // Start of this range (as a page index)
    atomic_size_t size;          // Number of pages in this range
    atomic_uintptr_t next[SKIPLIST_MAX_LEVEL]; // Forward pointers for coalescing skiplist
    atomic_uintptr_t prev[SKIPLIST_MAX_LEVEL]; // Back pointers for coalescing skiplist
    atomic_uintptr_t next_size[SKIPLIST_MAX_LEVEL]; // Forward pointers for size skiplist
    atomic_uintptr_t prev_size[SKIPLIST_MAX_LEVEL]; // Back pointers for size skiplist
    atomic_uint_least32_t alloc_count;
    atomic_uint_least32_t dealloc_count;
} range_node_t;

typedef struct {
    range_node_t head;
    atomic_flag write_lock;     // We can support arbitarily many readers, but only one writer
} skiplist_t;

size_t              quickpool_segment_size;
uint8_t            *mem_start;

static atomic_size_t       free_pages;
static atomic_flag         alloc_lock = ATOMIC_FLAG_INIT; // If quickpools are full
static quickpool_t         quickpools[QUICKPOOL_TOTAL] = {0};
static skiplist_t          free_ranges = { .head = {0}, .write_lock = ATOMIC_FLAG_INIT };

static size_t              pages;
static size_t              total_size;
static uint8_t            *page_table;
static uint8_t            *mem_end;
static uint8_t            *pages_start;
static uint8_t            *pages_end;

/* A comment to unconfuse clang-format */

static void quickpool_free(void *ptr, size_t size, size_t numb) {
    // Determine what section of memory this block is from
    uint32_t section = quickpool_offset(ptr);

    quickpool_push(&quickpools[0].subpool[section], ptr, numb);
}

static void* quickpool_alloc(size_t size) {
    for (int i = 0; i < QUICKPOOL_DIVISIONS; ++i) {
        void* item = quickpool_pop(&quickpools[0].subpool[i]);
        if (item) {
            //printf("%p: Got item from quickpool subdivision %i\n", pthread_self(), i);
            return item;
        }
        //printf("%p: Did not get item from quickpool subdivision %i, size: %zi\n", pthread_self(), i, atomic_load(&quickpools[0].subpool[i].size));
    }
    return NULL;
}

size_t get_free_pages() {
    return atomic_load(&free_pages);
}

size_t get_pages() {
    return pages;
}

__attribute__((always_inline)) static inline uint8_t type_data_pack(uint8_t type, uint8_t data) {
    type = type & 0x0F;
    data = data & 0x0F;

    return (type << 4) | data;
}

__attribute__((always_inline)) static inline uint8_t type_data_get_type(uint8_t packed) {
    return (packed >> 4) & 0x0F;
}

__attribute__((always_inline)) static inline uint8_t type_data_get_data(uint8_t packed) {
    return packed & 0x0F;
}

uint8_t get_page_type(size_t index) {
    return type_data_get_type(page_table[index]);
}

uint8_t get_page_data(size_t index) {
    return type_data_get_data(page_table[index]);
}

__attribute__((always_inline)) static inline uint8_t get_page_type_data(size_t index) {
    return page_table[index];
}

__attribute__((always_inline)) static inline void set_page_type_data(size_t index, enum allocator_type type, uint8_t data) {
    uint8_t type_data = type_data_pack(type, data);
    page_table[index] = type_data;
}

size_t get_page_index(void *ptr) {
    return ((size_t)ptr - (size_t)pages_start) / PAGE_SIZE;
}

void *get_page_by_index(size_t index) {
    return pages_start + (index * PAGE_SIZE);
}

static void page_table_initialize() {
    for (size_t i = 0; i < pages; i++) {
        page_table[i] = 0;
    }
}

static atomic_flag skiplist_print_lock = ATOMIC_FLAG_INIT;

void print_size_skiplist() {
    SPIN_LOCK_LOCK(skiplist_print_lock);
    printf("Size skiplist, free_pages: %zi\n", free_pages);

    for (int i = SKIPLIST_MAX_LEVEL - 1; i >= 0; i--) {
        range_node_t *current = &free_ranges.head;
        printf("Level %i: ", i);
        size_t count = 0;
        while (current) {
            printf("%p<(%p)>%p(%zi-%zi:%zi) -> ", (void*)current->prev_size[i], (void*)current, (void*)current->next_size[i], current->start, current->start + current->size, current->size);
            if (current == (void*)current->next_size[i]) {
                printf("duplicate node %p\n", current);
                exit(1);
            }
            if (current == (range_node_t *)current->next_size[i]) {
                printf("DUP!\n");
                exit(1);
            }
            current = (range_node_t *)current->next_size[i];
            if (count >= free_pages + 10) {
                printf("LOOP!\n");
                exit(1);
            }
            ++count;
        }
        printf("\n");
    }
    printf("Quickpools:\n");
    for (int i = 0; i < QUICKPOOL_DIVISIONS; ++i) {
       printf("quickpool division %i, size: %zi, head: %p\n", i, quickpools[0].subpool[i].size, (void*)quickpools[0].subpool[i].head);
    }
    SPIN_LOCK_UNLOCK(skiplist_print_lock);
}

static void print_index_skiplist() {
    printf("Index skiplist\n");

    for (int i = SKIPLIST_MAX_LEVEL - 1; i >= 0; i--) {
        range_node_t *current = &free_ranges.head;
        printf("Level %i: ", i);
        while (current) {
            printf("%p<(%p)>%p(%zi-%zi:%zi) -> ", (void*)current->prev[i], (void*)current, (void*)current->next[i], current->start, current->start + current->size, current->size);
            if (current == (void*)current->next[i]) {
                printf("duplicate node %p\n", current);
                exit(1);
            }
            current = (range_node_t *)current->next[i];
        }
        printf("\n");
    }
}

__attribute__((always_inline)) static inline uint32_t hash_32bit(uint32_t x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

__attribute__((always_inline)) static inline uint64_t hash_64bit(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    x = x ^ (x >> 31);
    return x;
}

__attribute__((always_inline)) static inline uintptr_t hash_pointer(void* ptr) {
    if (sizeof(void*) == 4)
        return ((uintptr_t)hash_32bit((uint32_t)ptr));

    return ((uintptr_t)hash_64bit((uint64_t)ptr));
}

__attribute__((always_inline)) static inline uint8_t determine_node_height(void *ptr) {
    uintptr_t hash = hash_pointer(ptr);
    int level = 1;

    while ((hash & 1) == 0 && level < SKIPLIST_MAX_LEVEL) {
        level++;
        hash >>= 1;
    }

    return level;
}

static inline void remove_range_size(range_node_t *node) {
   //printf("Size remove: %p before:\n", node);
   //print_size_skiplist();
   for (uint32_t i = 0; i < SKIPLIST_MAX_LEVEL; ++i) {
       range_node_t* next_size = (range_node_t*)node->next_size[i];
       range_node_t* prev_size = (range_node_t*)node->prev_size[i];

       if (next_size) {
           //printf("Remove updating next: %p\n", next_size);
           atomic_store_explicit(&next_size->prev_size[i], (uintptr_t)prev_size, memory_order_release);
       }

       if (prev_size) {
           //printf("Remove updating prev: %p\n", prev_size);
           atomic_store_explicit(&prev_size->next_size[i], node->next_size[i], memory_order_release);
       }
   }
   //printf("Size remove: %p after:\n", node);
   //print_size_skiplist();
}

static inline void remove_range(range_node_t *node) {
   //printf("Remove: %p before:\n", node);
   //print_size_skiplist();
   for (uint32_t i = 0; i < SKIPLIST_MAX_LEVEL; ++i) {
       range_node_t* next = (range_node_t*)node->next[i];
       range_node_t* prev = (range_node_t*)node->prev[i];
       range_node_t* next_size = (range_node_t*)node->next_size[i];
       range_node_t* prev_size = (range_node_t*)node->prev_size[i];

       if (next == node || next_size == node) {
           printf("Fatal: loop on node %p\n");
           exit(1);
       }

       if (next) {
           atomic_store_explicit(&next->prev[i], (uintptr_t)prev, memory_order_release);
       }
       if (next_size) {
           //printf("Updating next: %p\n", next_size);
           atomic_store_explicit(&next_size->prev_size[i], (uintptr_t)prev_size, memory_order_release);
       }

       if (prev) {
           atomic_store_explicit(&prev->next[i], node->next[i], memory_order_release);
       }
       if (prev_size) {
           //printf("Updating prev: %p\n", prev_size);
           atomic_store_explicit(&prev_size->next_size[i], node->next_size[i], memory_order_release);
       }
   }
   //printf("Remove: %p after:\n", node);
   //print_size_skiplist();
}

static bool skip_list_remove(range_node_t *node) {
    // We should be locked by the caller. If not: Good luck!

    // Make sure we don't confuse anyone
    uint32_t expected = RANGE_MAGIC;
    uint32_t desired = 0;

    if (!atomic_compare_exchange_strong(&node->magic, &expected, desired)) {
        return false;
    }
    atomic_fetch_add(&node->dealloc_count, 1);
    remove_range(node);
    return true;
}

__attribute__((always_inline)) static inline int compare_range_size_index(range_node_t* a, range_node_t* b) {
    if (a->size < b->size) return -1;
    if (a->size > b->size) return 1;

    if (a->start < b->start) return -1;
    if (a->start > b->start) return 1;

    printf("compare_range duplicate %p:%p\n", a, b);
    //print_size_skiplist();
    exit(1);
    return 0;  // Both size and index are equal
}

static inline void insert_node_size(skiplist_t *list, range_node_t* node) {
    range_node_t *update[SKIPLIST_MAX_LEVEL];
    range_node_t *current = &list->head;

    //printf("Size insert: %p at level %i\n", node, node->level);

    // Find the place to insert the new node
    for (int i = SKIPLIST_MAX_LEVEL - 1; i >= 0; i--) {
        while (current->next_size[i] && compare_range_size_index((range_node_t*)current->next_size[i], node) < 0) {
            if (current == (void*)current->next_size[i]) {
                printf("size list: duplicate node %p\n", current);
                exit(1);
            }
            current = (range_node_t *)current->next_size[i];
        }
        update[i] = current;
    }

    for (uint32_t i = 0; i < node->level + 1; i++) {
        //printf("Updating: %p\n", update[i]);
        range_node_t* next = (range_node_t*)update[i]->next_size[i];
        if (next == node && next->size == 0) {
            printf("size list: trying to create a loop on level %i %p\n", i, node);
            exit(1);
        }
        if (next) {
            atomic_store_explicit(&next->prev_size[i], (uintptr_t)node, memory_order_release);
        }
        atomic_store_explicit(&update[i]->next_size[i], (uintptr_t)node, memory_order_release);
        atomic_store_explicit(&node->prev_size[i], (uintptr_t)update[i], memory_order_release);
        atomic_store_explicit(&node->next_size[i], (uintptr_t)next, memory_order_release);
    }

    //print_size_skiplist();
}

static inline void reinsert_node_size(skiplist_t *list, range_node_t* node, size_t new_size) {
    //printf("Size re-insert: %p\n", node);
    // Make sure we temporarily prevent this node frome being found
    atomic_store(&node->size, 0);
    remove_range_size(node);
    atomic_store(&node->size, new_size);
    insert_node_size(list, node);
}

#include <signal.h>

static void insert_range_sorted(skiplist_t *list, range_node_t *node, size_t start_index, size_t size, bool coalesce) {
    // We should be locked by the caller. If not: Good luck!

    // place the structure in the middle of the page
    node = (void*)((uintptr_t)node + PAGE_SIZE / 2);

    if (atomic_load(&node->magic) == RANGE_MAGIC) {
        printf("FATAL: Duplicate free? %p: alloc_count: %i, dealloc_count: %i\n", node, atomic_load(&node->alloc_count), atomic_load(&node->dealloc_count));
        printf("-----------------------------------\n");
        print_size_skiplist();
        printf("-----------------------------------\n");
        print_index_skiplist();
        printf("-----------------------------------\n");
        raise(SIGINT);
        exit(1);
        return;
    }

    atomic_fetch_add(&node->alloc_count, 1);
    
    range_node_t *update[SKIPLIST_MAX_LEVEL];
    range_node_t *current = &list->head;

    // This is fine as the spin lock also issued an acquire fence
    for (int i = SKIPLIST_MAX_LEVEL - 1; i >= 0; i--) {
        while ((range_node_t *)current->next[i] != NULL && (range_node_t *)current->next[i] < node) {
            if (current == (void*)current->next[i]) {
                printf("index list: duplicate node %p\n", current);
                exit(1);
            }
            current = (range_node_t *)current->next[i];
        }
        update[i] = current;
    }

    // We now have a pointer to the place right before where we want to insert at each level
    
    range_node_t *next_node = (range_node_t*)update[0]->next[0];
    while (coalesce && next_node) {
        if (start_index + size == next_node->start) {
            // Our new range extends into the next node
            size += next_node->size;
            //printf("Extending into %p\n", next_node);
            if (!skip_list_remove(next_node)) {
                printf("skip_list_remove failed during coalesce\n");
                exit(1);
            }
            next_node = (void*)next_node->next[0];
        } else {
            break;
        }
    }

    // at this point the update array is filled with the nodes prior to this node
    // on level 0 we have the immediate neighboring new nodes. If the prior node on
    // level 0 ends where we begin then don't need to insert
    if (coalesce && update[0] != &list->head && update[0]->start + update[0]->size == start_index) {
        //printf("Expanding %p into our space %p start_index: %zi, prior end: %zi, prior size: %zi, new size: %zi\n", update[0], node, start_index, update[0]->start + update[0]->size, update[0]->size, update[0]->size + size);
        // This range extends the prior range
        reinsert_node_size(list, update[0], update[0]->size + size);
        //atomic_fetch_add(&update[0]->size, size);
        return;
    }

    //printf("Inserting range starting at %p start_index %zi size %zi coalesce: %i before:\n", node, start_index, size, coalesce);
    //print_index_skiplist();

    // We are under write lock, so no other writer can be touching this node
    // we will be ussuing a thread fence again later
    node->start = start_index;
    node->size = size;
    node->magic = RANGE_MAGIC;

    // Make sure we don't have any weird pointers on other levels
    for (uint32_t i = 0; i < SKIPLIST_MAX_LEVEL; ++i) {
        node->next[i] = 0;
        node->prev[i] = 0;
        node->next_size[i] = 0;
        node->prev_size[i] = 0;
    }

    int level = determine_node_height(node);
    node->level = level - 1;
    // Make sure that this node is fully ready
    for (uint32_t i = 0; i < node->level + 1; i++) {
        node->next[i] = update[i]->next[i];
        node->prev[i] = (uintptr_t)update[i];
    }

    // All writes to the node should now be visible everywhere
    atomic_thread_fence(memory_order_release);

    //printf("Inserting %p at level %i\n", node, node->level);
    // Now update everyone else's pointers
    for (uint32_t i = 0; i < node->level + 1; i++) {
        //printf("Updating: %p\n", update[i]);
        range_node_t* next = (range_node_t*)update[i]->next[i];
        if (next == node) {
            printf("index list: trying to create a loop on level %i %p\n", i, node);
            exit(1);
        }
        if (next) {
            atomic_store_explicit(&next->prev[i], (uintptr_t)node, memory_order_release);
        }
        atomic_store_explicit(&update[i]->next[i], (uintptr_t)node, memory_order_release);
        //atomic_store_explicit(&node->prev[i], (uintptr_t)update[i], memory_order_release);
        //atomic_store_explicit(&node->next[i], (uintptr_t)next, memory_order_release);
    }
    insert_node_size(list, node);
    //print_index_skiplist();
    //printf("Inserting range starting at %p start_index %zi size %zi coalesce: %i after:\n", node, start_index, size, coalesce);
    //print_index_skiplist();
}

static range_node_t* skiplist_get_largest_node(skiplist_t* list) {
    int level = SKIPLIST_MAX_LEVEL - 1;
    range_node_t *current = &list->head;

    // Traverse down and right, as far as possible
    while (level >= 0) {
        while (true) {
            range_node_t *next_node = (range_node_t *)atomic_load(&current->next_size[level]);
            if (next_node) {
                current = next_node;
            } else {
                break; // We're at the rightmost node of this level
            }
        }
        level--;
    }

    // Once we're at level 0 and at the rightmost node, check its size
    size_t current_size = atomic_load(&current->size);
    if (current_size == 0) {
        // Get the previous node since this one has size 0
        current = (range_node_t *)atomic_load(&current->prev_size[0]);
    }
    
    if (current != &list->head) {
        return current;
    } else {
        return NULL;
    }
}

static void* skiplist_get_firstfit(skiplist_t* list, size_t size) {
    range_node_t *update[SKIPLIST_MAX_LEVEL];
    range_node_t *current = &list->head;
    range_node_t *node = NULL;

    for (int i = SKIPLIST_MAX_LEVEL - 1; i >= 0; i--) {
        while ((node = (void*)atomic_load_explicit(&current->next_size[i], memory_order_relaxed))) {
            size_t node_size = atomic_load_explicit(&node->size, memory_order_relaxed);
            if (node_size && node_size >= size) {
                break;
            }
            current = node;
        }
    }

    if (node != &list->head) {
        return node;
    } else {
        return NULL;
    }
}

static size_t skiplist_get_largest_size(skiplist_t* list) {
    range_node_t *node = skiplist_get_largest_node(list);

    //print_size_skiplist();
    //print_index_skiplist();
    if (node)
        return node->size;
    return 0;
}

size_t get_largest_size() {
    return skiplist_get_largest_size(&free_ranges);
}

static void* skiplist_get_pages(skiplist_t* list, size_t *size, size_t slack, bool at_end) {
    SPIN_LOCK_LOCK(list->write_lock);
start:
    //printf("Attempting to find a range of size %zi with slack %zi\n", *size, slack);
    void* page = NULL;
    range_node_t* current = NULL;

    if (*size >= pages / 8) {
        // Worst fit for large allocations
        current = skiplist_get_largest_node(list);
    } else {
        // Best fit for smaller allocations
        current = skiplist_get_firstfit(list, *size - slack);
    }

    if (!current || current == &list->head) {
        // We didn't find anything
        //return NULL;
        goto out;
    }

    if (atomic_load_explicit(&current->magic, memory_order_relaxed) != RANGE_MAGIC) {
        // Someone raced us here, retr
        SPIN_LOCK_UNLOCK(list->write_lock);
        goto start;
    }

    page = ALIGN_PAGE_DOWN(current);
    size_t new_size = 0;
    size_t current_size = atomic_load(&current->size);
    if (current_size >= *size) {
        if (current_size >= *size + slack) {
            new_size = current_size - *size;
        } else {
            *size = current_size;
        } 
    } else {
        if (current_size < *size - slack) {
            //printf("Got range of size %zi, requested %zi slack %zi\n", current->size, *size, slack);
            page = NULL;
            goto out;
        }
        *size = current_size;
    }

    void* new_node = NULL;
    if (at_end) {
        page = (void*)((uintptr_t)page + (PAGE_SIZE * new_size));
        if (new_size) {
            //print_size_skiplist();
            reinsert_node_size(list, current, new_size);
            //print_size_skiplist();

            goto out;
        }
    } else {
        new_node = (void*)((uintptr_t)page + (PAGE_SIZE * *size));
        if (new_size) {
            insert_range_sorted(list, new_node, get_page_index(new_node), new_size, false);
        }
    }

    if (!skip_list_remove((void*)current)) {
        printf("skip_list_remove failed during allocation\n");
        exit(1);
    }

    // printf("Found range of size %zi at index %zi in a block of %zi %p\n", size, ((range_node_t *)current->next[0])->start, ((range_node_t *)current->next[0])-size, node);

out:
    SPIN_LOCK_UNLOCK(list->write_lock);
    return page;
}

void quickpool_destroy(size_t size) {
    (void)size; // unused right now
    
    for (uint32_t i = 0; i < QUICKPOOL_DIVISIONS; ++i) {
        void *ptr;
        while((ptr = quickpool_pop(&quickpools[0].subpool[i]))) {
            insert_range_sorted(&free_ranges, ptr, get_page_index(ptr), 1, true);
        }
    }
}

static void quickpool_empty(uint32_t division) {
    void *ptr;
    size_t count = 0;
    while((ptr = quickpool_pop(&quickpools[0].subpool[division]))) {
        SPIN_LOCK_LOCK(free_ranges.write_lock);
        insert_range_sorted(&free_ranges, ptr, get_page_index(ptr), 1, true);
        ++count;
        SPIN_LOCK_UNLOCK(free_ranges.write_lock);
    }

    //printf("Emptied %zi items from quickpool %i\n", count, division);
}

void page_alloc_init(void *start, void *end) {
    mem_start              = (uint8_t *)start;
    mem_end                = (uint8_t *)end;
    total_size             = (size_t)end - (size_t)start;
    quickpool_segment_size = (total_size + QUICKPOOL_DIVISIONS - 1) / QUICKPOOL_DIVISIONS;

    uint8_t *first_page    = ALIGN_PAGE_UP(mem_start);
    pages_end              = ALIGN_PAGE_DOWN((mem_end));
    pages                  = (((size_t)pages_end) - ((size_t)first_page)) / PAGE_SIZE;

    size_t page_table_size = pages;

    page_table             = mem_start;
    pages_start            = ALIGN_PAGE_UP(((char *)page_table) + page_table_size);

    pages = (((size_t)pages_end) - ((size_t)pages_start)) / PAGE_SIZE; // Need to recaculate the number of pages now
    free_pages = pages;

    page_table_initialize();
    insert_range_sorted(&free_ranges, (range_node_t*)pages_start, 0, pages, false);
    print_size_skiplist();

#ifndef BADGEROS_KERNEL
    size_t waste = (((size_t)pages_start - (size_t)mem_start)) - (page_table_size);

    printf(
        "Memory starts at: %p, Memory ends at: %p, First page at: %p, "
        "total_pages: %zi, page_table_size: %zi, waste: %zi, usable memory: %zi\n",
        start,
        end,
        pages_start,
        pages,
        page_table_size,
        waste,
        pages * PAGE_SIZE
    );
#endif
}

#define MAX_SIZE_T_BITS (sizeof(size_t) * 8)
#define MAX_ENCODED_PAGES (MAX_SIZE_T_BITS / 4)

static inline uint8_t encode_link_size(size_t allocation_size, size_t chunk_index) {
    if (allocation_size >= 2 && allocation_size <= 16) {
        if (chunk_index == 0) {
            return (uint8_t)(allocation_size - 2);  // 0 represents size 1, 1 represents size 2, ..., 15 represents size 16
        } else {
            return 0;
        }
    } else {
        if (chunk_index == 0) {
            return 0xF;  // Sentinel value
        } else {
            return (allocation_size >> (4 * (chunk_index - 1))) & 0xF;
        }
    }
}

void *page_alloc_link(size_t size) {
    if (size < 2) {
        return NULL;
    }

    //printf("Allocating a range of size %zi\n", size);

    uint8_t tries = 0;
start:
    if (atomic_load(&free_pages) < size) {
        return NULL;
    }

    SPIN_LOCK_LOCK(alloc_lock);
    size_t new_size = size;
    void* range = skiplist_get_pages(&free_ranges, &new_size, 0, false);
    //printf("Range first try: %p\n", range);
    if (!range) {
        //printf("Got no range, emptying quickpools\n");
        for (int i = QUICKPOOL_DIVISIONS - 1; i >= 0; --i) {
            //printf("-------------- Emptying quickpool %i\n", i);
            //print_size_skiplist();
            quickpool_empty(i);
            //printf("-------------- Emptying quickpool %i after:\n", i);
            //print_size_skiplist();
            //printf("----------------------------------------\n");

            new_size = size;
            range = skiplist_get_pages(&free_ranges, &new_size, 0, true);
            if (range) {
                break;
            }
        }
    }

    if (!range) {
        if (tries > 2) {
            //printf("Got no range after 5 tries\n");
            goto out;
        } else {
            ++tries;
            //printf("Got no range after %i tries, retrying\n", tries);
            SPIN_LOCK_UNLOCK(alloc_lock);
            DELAY(tries);
            goto start;
        }
    }

    //printf("Got a range of size %zi\n", size);

    for(size_t i = 0; i < size; ++i) {
        void* page = (void*)((uintptr_t)range + (i * PAGE_SIZE));
        size_t page_index = get_page_index(page);
        uint8_t data = encode_link_size(size, i);
        if (i <= MAX_ENCODED_PAGES) {
            //printf("Setting page data to %X for size %zi:%zi page_index: %zi\n", data, i, size, page_index);
        }
        set_page_type_data(page_index, ALLOCATOR_PAGE_LINK, data);
    }

    atomic_fetch_sub(&free_pages, size);
    //printf("Allocated %p for size %zi\n", range, size);
out:
    SPIN_LOCK_UNLOCK(alloc_lock);
    //printf("Allocating page link %p of size %zi\n", range, size);
    return range;
}

void page_free_link(void *ptr) {
    if (!ptr)
        return;

    size_t size = 0;
    size_t index = 0;
    bool reading_large_size = false;
    int chunk_index = 0;

    //printf("----------------------\n");
    while (true) {
        void* page = (void*)((uintptr_t)ptr + (index * PAGE_SIZE));
        size_t page_index = get_page_index(page);
        uint8_t data = get_page_type_data(page_index);
        uint8_t size_part = type_data_get_data(data);

        if (chunk_index <= MAX_ENCODED_PAGES) {
            //printf("Getting page data %X for size %zi:%zi page_index: %zi\n", size_part, chunk_index, size, page_index);
        }

        if (!reading_large_size) {
            if (size_part < 0xF) {
                size = size_part + 2; // 0 represents size 1, 1 represents size 2, ..., 15 represents size 16
                break;
            } else {
                reading_large_size = true;
            }
        } else {
            size |= ((size_t)size_part << (4 * chunk_index));
            chunk_index++;

            if (chunk_index >= MAX_ENCODED_PAGES - 1) {
                // We've read all the possible chunks for a large size
                break;
            }
        }

        index++;
    }

    //printf("Freeing page link %p of size %zi\n", ptr, size);
    SPIN_LOCK_LOCK(free_ranges.write_lock);
    insert_range_sorted(&free_ranges, ptr, get_page_index(ptr), size, true);
    SPIN_LOCK_UNLOCK(free_ranges.write_lock);
    atomic_fetch_add(&free_pages, size);
}

void *page_alloc(enum allocator_type type, uint8_t data) {
    uint8_t tries = 0;
start:
    void* page = quickpool_alloc(1);
    if (!page) {
        if (SPIN_LOCK_TRY_LOCK(alloc_lock)) {
            size_t size = pages / 16;
            //size_t size = 1;
            void* range = skiplist_get_pages(&free_ranges, &size, size - 1, false);
            if (range) {
                //void* empty_range = (void*)((uintptr_t)range + PAGE_SIZE);
                //printf("Got a range of size %zi after %i tries\n", size, tries);
                for (size_t i = 1; i < size; ++i) {
                    quickpool_free((void*)((uintptr_t)range + (i * PAGE_SIZE)), 1, 1);
                }
                //if (size > 1) {
                //    quickpool_free(empty_range, 1, size - 1);
                //}
            } else {
                ++tries;

                // If someone just allocated ahead of us we might have just missed it
                if (tries < 5) {
                    SPIN_LOCK_UNLOCK(alloc_lock);
                    goto start;
                }

                //printf("%p: No ranges found?\n", pthread_self());
                //
                //for (int i = 0; i < QUICKPOOL_DIVISIONS; ++i) {
                //    printf("quickpool division %i, size: %zi\n", i, quickpools[0].subpool[i].size);
                //}
            } 
            page = range;
            SPIN_LOCK_UNLOCK(alloc_lock);
        } else {
            goto start;
        }
    }

    if (page) {
        atomic_fetch_sub(&free_pages, 1);
        size_t page_index = get_page_index(page);
        set_page_type_data(page_index, type, data);
    }

    //printf("Allocated page %p\n", page);
    return page;
}

void page_free(void *ptr) {
    if (!ptr) {
        return;
    }

    //printf("Freeing page %p\n", ptr);
    quickpool_free(ptr, 1, 1);
    //printf("Free: %p\n", ptr);
    atomic_fetch_add(&free_pages, 1);
    //SPIN_LOCK_LOCK(free_ranges.write_lock);
    //insert_range_sorted(&free_ranges, ptr, get_page_index(ptr), 1, true);
    //SPIN_LOCK_UNLOCK(free_ranges.write_lock);
    //printf("End Free: %p\n", ptr);
}
