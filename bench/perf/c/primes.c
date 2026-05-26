#include <stdio.h>

long is_prime(long n) {
    if (n < 2) return 0;
    if (n == 2) return 1;
    if (n % 2 == 0) return 0;
    long i = 3;
    while (i*i <= n) {
        if (n % i == 0) return 0;
        i += 2;
    }
    return 1;
}

int main() {
    long c = 0;
    for (long k = 2; k <= 500000L; k++) {
        c += is_prime(k);
    }
    printf("%ld\n", c);
    return 0;
}
