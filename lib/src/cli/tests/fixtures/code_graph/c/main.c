#include <stdio.h>

#include "store.h"

static int clamp(int value) {
    return value > 9 ? 9 : value;
}

int main(void) {
    struct store s = {0};
    store_add(&s, clamp(3));
    printf("%d\n", store_total(&s));
    return 0;
}
