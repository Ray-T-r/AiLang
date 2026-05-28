#include <stdio.h>

int main(void) {
    int count = 0;
    int total = 2;
    char rec[128];
    for (int i = 0; i < 50000; i++) {
        int age = 18 + i % 52;
        if (age >= 40) {
            int n = snprintf(rec, sizeof(rec), "{\"id\":%d,\"name\":\"user_%d\",\"age\":%d}", i, i, age);
            if (count > 0) total += 1;
            total += n;
            count += 1;
        }
    }
    printf("%d\n%d\n", count, total);
    return 0;
}
