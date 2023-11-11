#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "wrap.h"
#include "pid_sim.h"

extern address_range* function_range;

void add_function(void* start) {
    address_range* r = function_range;
    uint16_t id = 0;

    if (!r) {
        //printf("Allocating from scratch\n");
        function_range = __real_malloc(sizeof(address_range));
        r = function_range;
        id = 1;
    } else {
        //printf("Allocating new node\n");
        while (r->next) {
            r = r->next;
        }

        id = r->id;
        r->next = __real_malloc(sizeof(address_range));
        r = r->next;
        id += 1;
    }

    //printf("Adding node: %p, id: %i\n", start, id);
    r->start = (uintptr_t)start;
    r->id = id;
    r->next = NULL;
}

void free_range(address_range* range) {
    if (range->next) {
        free_range(range->next);
    }
    __real_free(range);
}

uint16_t find_pid(void* start) {
    //printf("Finding address %p\n", start);
    address_range* r = function_range;

    while (r->next) {
        if ((uintptr_t)start >= r->start && (uintptr_t)start < r->next->start) {
            return r->id;
        }

        r = r->next;
    }

    return 0;
}
