#include <time.h>
#include <stdio.h>

#include "bitops.h"

int main() {
    clock_t start_test, end_test, start_native, end_native, start_software, end_software;
    uint64_t time_test, time_native, time_software;

    start_test = clock();
    for (uint32_t i = 1; i < UINT32_MAX; ++i) {
        if (clz32(i) != __builtin_clz(i)) {
            printf("clz32 fail at %i\n", i);
        }
        if (ctz32(i) != __builtin_ctz(i)) {
            printf("ctz32 fail at %i\n", i);
        }
        if (popcount32(i) != __builtin_popcount(i)) {
            printf("popcount32 fail at %i\n", i);
        }
        if (ffs32(i) != __builtin_ffs(i)) {
            printf("ffs fail at %i\n", i);
        }
    }
    end_test = clock();

    start_native = clock();
    for (uint32_t i = 1; i < UINT32_MAX; ++i) {
        __builtin_clz(i);
        __builtin_ctz(i);
        __builtin_popcount(i);
        __builtin_ffs(i);
    }
    end_native = clock();

    start_software = clock();
    for (uint32_t i = 1; i < UINT32_MAX; ++i) {
        clz32(i);
        ctz32(i);
        popcount32(i);
        ffs32(i);
    }
    end_software = clock();

    time_test = end_test - start_test;
    time_native = end_native - start_native;
    time_software = end_software - start_software;

    printf("Test: %li, native: %li, software: %li, difference: %li\n", time_test, time_native, time_software, time_software - time_native);
}
