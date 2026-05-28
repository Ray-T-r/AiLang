#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct { const char* key; int klen; long val; } Entry;

int main(void) {
    const char* seed = "the quick brown fox jumps over the lazy dog ";
    size_t slen = strlen(seed);
    size_t reps = 500000;
    size_t tlen = slen * reps;
    char* text = (char*) malloc(tlen + 1);
    for (size_t i = 0; i < reps; i++) memcpy(text + i * slen, seed, slen);
    text[tlen] = '\0';

    Entry entries[32];
    int n = 0;
    size_t pos = 0;
    while (pos <= tlen) {
        size_t end = pos;
        while (end < tlen && text[end] != ' ') end++;
        int wlen = (int)(end - pos);
        int found = -1;
        for (int i = 0; i < n; i++) {
            if (entries[i].klen == wlen && memcmp(entries[i].key, text + pos, wlen) == 0) {
                found = i; break;
            }
        }
        if (found < 0) {
            entries[n].key = text + pos;
            entries[n].klen = wlen;
            entries[n].val = 1;
            n++;
        } else {
            entries[found].val++;
        }
        if (end >= tlen) break;
        pos = end + 1;
    }

    printf("%d\n", n);
    for (int i = 0; i < n; i++) {
        if (entries[i].klen == 3 && memcmp(entries[i].key, "the", 3) == 0) {
            printf("%ld\n", entries[i].val);
            break;
        }
    }
    free(text);
    return 0;
}
