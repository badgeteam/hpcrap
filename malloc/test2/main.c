#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern void kernel_heap_init();
extern void print_heap();

void *alloc(char n, size_t size) {
	char* t = malloc(size);
	memset(t, n, size);
	t[size - 1] = 0;

	return t;
}

int main() {
	kernel_heap_init();
	//print_heap();

	char* y = alloc('y', 100);
	char* x = alloc('x', 100);
	char* z = NULL;

	printf("x: %p y: %p z: %p\n", x, y, z);
	printf("x: %s\n", x);
	printf("y: %s\n", y);
	printf("z: %s\n", z);
	free(x);
	//print_heap();
	printf("-------------\n");

	x = alloc('x', 100);
	printf("x: %p y: %p z: %p\n", x, y, z);
	printf("x: %s\n", x);
	printf("y: %s\n", y);
	printf("z: %s\n", z);
	free(x);
	//print_heap();
	printf("-------------\n");

	z = alloc('z', 50);
	x = alloc('x', 50);
	printf("x: %p y: %p z: %p\n", x, y, z);
	printf("x: %s\n", x);
	printf("y: %s\n", y);
	printf("z: %s\n", z);
	free(x);
	free(z);
	//print_heap();
	z = NULL;
	printf("-------------\n");

	x = alloc('x', 100);
	z = alloc('z', 50);
	printf("x: %p y: %p z: %p\n", x, y, z);
	printf("x: %s\n", x);
	printf("y: %s\n", y);
	printf("z: %s\n", z);
	free(x);
	free(z);
	//print_heap();
	z = NULL;
	printf("-------------\n");

	x = alloc('x', 50);
	printf("x: %p y: %p z: %p\n", x, y, z);
	printf("x: %s\n", x);
	printf("y: %s\n", y);
	printf("z: %s\n", z);

	printf("-------------\n");
	x = realloc(x, 100);
	printf("x: %p y: %p z: %p\n", x, y, z);
	printf("x: %s\n", x);
	printf("y: %s\n", y);
	printf("z: %s\n", z);

	memset(x + 49, '*', 50);
	x[99] = '\0';
	printf("x: %s\n", x);
	x = realloc(x, 5);
	x[4] = '\0';
	printf("x: %s\n", x);

	free(x);
	print_heap();
	printf("-------------\n");
}
