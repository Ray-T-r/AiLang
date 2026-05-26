#include <stdio.h>
#include <string.h>

void greet(char* out, const char* name) {
    sprintf(out, "Hello, %s!", name);
}

int main() {
    char buf[64];
    greet(buf, "C");
    printf("%s\n", buf);
    return 0;
}
