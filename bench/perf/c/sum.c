#include <stdio.h>

int main() {
    long s = 0;
    for (long i = 1; i <= 100000000L; i++) {
        s += i;
    }
    printf("%ld\n", s);
    return 0;
}
