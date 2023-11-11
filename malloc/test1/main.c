#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "wrap.h"

#include "pid_sim.h"
address_range* function_range = NULL;

void test1() {
    char* x = malloc(100);
    free(x);
}

void test2() {
    char* x = malloc(120);
    free(x);
}

void dummy() {
}

int main() {
    add_function(test1);
    add_function(test2);
    add_function(dummy);

    test1();
    test2();

    free_range(function_range);
}
