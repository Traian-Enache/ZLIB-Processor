#ifndef _ZHEAP_H
#define _ZHEAP_H

#include "huffman.h"
#define ZHEAP_CAPACITY 512

typedef struct z_heap {
    huffman_tree *nodes[ZHEAP_CAPACITY];
    int priority[ZHEAP_CAPACITY];
    int size;
} z_heap;

z_heap *zheap_create();

void zheap_destroy(z_heap *zh);

void zheap_push(z_heap *zh, huffman_tree *node, int priority);

void zheap_pop(z_heap *zh);

huffman_tree *zheap_peek(z_heap *zh);

int zheap_is_empty(z_heap *zh);

int zheap_get_size(z_heap *zh);

#endif  // _ZHEAP_H
