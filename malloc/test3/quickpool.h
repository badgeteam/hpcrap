

extern size_t quickpool_segment_size;
extern uint8_t* mem_start;

typedef struct quickpool_node {
  atomic_uintptr_t next;
} node_t;

typedef struct {
  atomic_uintptr_t head;
  atomic_size_t size;
} pool_t;

typedef struct {
  pool_t subpool[QUICKPOOL_DIVISIONS];
} quickpool_t;

/* A comment to unconfuse clang-format */

static inline uint32_t quickpool_offset(void* pointer) {
    size_t offset = (size_t)pointer - (size_t)mem_start;
    return offset / quickpool_segment_size;
}

static inline void quickpool_push(pool_t* pool, node_t* ptr, size_t numb) {
    // offset node somewhere in the page other than where other structures are
    node_t* node_head = (void*)((uintptr_t)ptr + PAGE_SIZE / 4);
    node_t* node_tail = (void*)((uintptr_t)ptr + ((numb - 1) * PAGE_SIZE) + PAGE_SIZE / 4);

    for (size_t i = 0; i < numb ; ++i) {
        node_t* node = (void*)((uintptr_t)ptr + (i * PAGE_SIZE) + PAGE_SIZE / 4);
        node_t* node_next = (void*)((uintptr_t)ptr + ((i + 1) * PAGE_SIZE) + PAGE_SIZE / 4);
        node->next = node_next;
    }

    node_t* old_head = NULL;
    old_head = (void*)atomic_load(&pool->head);
    do {
        atomic_store(&node_tail->next, (uintptr_t)old_head);
    } while (!atomic_compare_exchange_weak(&pool->head, &old_head, (uintptr_t)node_head));

    atomic_fetch_add(&pool->size, numb);
}

static inline void* quickpool_pop(pool_t* pool) {
    node_t* old_head = NULL;
    node_t* new_head = NULL;

    old_head = (void*)atomic_load(&pool->head);
    do {
        if (!old_head) {
            return NULL;
        }
        new_head = (void*)atomic_load(&old_head->next);
    } while (!atomic_compare_exchange_weak(&pool->head, &old_head, (uintptr_t)new_head));

    if (old_head) {
        atomic_fetch_sub(&pool->size, 1);
    }

    return ALIGN_PAGE_DOWN(old_head);
}

