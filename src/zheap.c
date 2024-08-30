#include "zheap.h"
#include <stdlib.h>
#include <assert.h>

#define ZH_GO_UP(x) (((x) - 1) >> 1)
#define ZH_GO_LEFT(x) (((x) << 1) + 1)
#define ZH_GO_RIGHT(x) (((x) << 1) + 2)
#define ZH_SWAP(zh, a, b) {\
    huffman_tree *tempn = zh->nodes[a];\
    zh->nodes[a] = zh->nodes[b];\
    zh->nodes[b] = tempn;\
    int tempp = zh->priority[a];\
    zh->priority[a] = zh->priority[b];\
    zh->priority[b] = tempp;}


static void zh_sift_up(z_heap *zh, int index)
{
	while (index != 0) {
		int parent = ZH_GO_UP(index);
		if (zh->priority[parent] <= zh->priority[index])
			break;
		ZH_SWAP(zh, parent, index);
		index = parent;
	}
}

static void zh_sift_down(z_heap *zh, int index)
{
	while (1) {
		int lowest = index;
		int l = ZH_GO_LEFT(index);
		int r = ZH_GO_RIGHT(index);
		if (l < zh->size && zh->priority[l] < zh->priority[index])
			lowest = l;
		if (r < zh->size && zh->priority[r] < zh->priority[lowest])
			lowest = r;
		if (index == lowest)
			break;
		ZH_SWAP(zh, index, lowest);
		index = lowest;
	}
}

z_heap *zheap_create()
{
    z_heap *zh = (z_heap *)calloc(1, sizeof(z_heap));
    assert(zh);

    return zh;
}

void zheap_destroy(z_heap *zh)
{
    free(zh);
}

void zheap_push(z_heap *zh, huffman_tree *node, int priority)
{
	if (!zh || zh->size >= ZHEAP_CAPACITY)
		return;

	zh->nodes[zh->size] = node;
	zh->priority[zh->size] = priority;

	int idx = zh->size++;
	zh_sift_up(zh, idx);
}

void zheap_pop(z_heap *zh)
{
	if (!zh || !zh->size)
		return;

	if (zh->size == 1) {
		zh->size--;
		return;
	}

	ZH_SWAP(zh, 0, zh->size - 1);

	zh->size--;

	zh_sift_down(zh, 0);
}

huffman_tree *zheap_peek(z_heap *zh)
{
    if (!zh || !zh->size)
        return NULL;
    return zh->nodes[0];
}

int zheap_is_empty(z_heap *zh)
{
    if (!zh || !zh->size)
        return 1;
    return 0;
}

int zheap_get_size(z_heap *zh)
{
    if (!zh)
        return 0;
    return zh->size;
}
