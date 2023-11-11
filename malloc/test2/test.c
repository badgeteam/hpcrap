#include <stdlib.h>

int main() {
  char* s = malloc(100);
  free(s);
  s = realloc(s, 50);
  free(s);
}
