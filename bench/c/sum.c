#include <stdio.h>

int main() {
    int s = 0;
    for (int i = 1; i < 101; i++) {
        s += i;
    }
    printf("%d\n", s);
    return 0;
}
