// 100k string concatenations — naive `acc = acc + "y"` to match the
// other languages' "what a normal programmer types" baseline.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main() {
    size_t cap = 16, len = 1;
    char* acc = (char*) malloc(cap);
    acc[0] = 'x'; acc[1] = '\0';
    for (int i = 1; i <= 100000; i++) {
        // Naive: always reallocate a fresh buffer of exact size and copy.
        char* next = (char*) malloc(len + 2);
        memcpy(next, acc, len);
        next[len] = 'y';
        next[len + 1] = '\0';
        free(acc);
        acc = next;
        len += 1;
    }
    printf("%zu\n", len);
    free(acc);
    return 0;
}
