#include "store.h"

static int clamp(int value) {
    return value < 0 ? 0 : value;
}

void store_add(struct store* s, int value) {
    s->count += clamp(value);
}

int store_total(const struct store* s) {
    return s->count;
}
