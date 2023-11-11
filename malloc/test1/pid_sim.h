#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct address_range_s {
    uint16_t id;
    uintptr_t start;
    struct address_range_s* next;
} address_range;

void add_function(void* start);
void free_range(address_range* range);
uint16_t find_pid(void* start);
