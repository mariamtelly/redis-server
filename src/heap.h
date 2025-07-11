#pragma once

#include <stddef.h>
#include <stdint.h>

struct HeapItem {
    uint64_t val;
    size_t *ref;
};

void heap_update(HeapItem *a, size_t pos, size_t len);