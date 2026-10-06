#ifndef STORE_H
#define STORE_H

#include <stddef.h>

struct store {
    int count;
};

void store_add(struct store* s, int value);
int store_total(const struct store* s);

#endif
