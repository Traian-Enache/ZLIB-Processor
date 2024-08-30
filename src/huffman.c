#include "huffman.h"

#include <stdlib.h>
#include <assert.h>
#include <memory.h>
#include <stdio.h>
#include "zheap.h"

#define MAX_BITS 24

static huffman_tree *hm_node_create(int value, int weight);

static inline __attribute__((__always_inline__)) int f_log2(int x)
{
	int res = -1;
	while (x) {
		x >>= 1;
		res++;
	}
	return res;
}

static int hm_count_leaves(huffman_tree *tree)
{
	if (!tree)
		return 0;
	if (HUFFMAN_IS_LEAF(tree))
		return 1;
	return hm_count_leaves(tree->left) + hm_count_leaves(tree->right);
}

static void hm_inorder(huffman_tree *root, int *leaf_symbols, int *leaf_cnt)
{
	if (!root)
		return;

	hm_inorder(root->left, leaf_symbols, leaf_cnt);

	if (HUFFMAN_IS_LEAF(root)) {
		leaf_symbols[(*leaf_cnt)++] = root->value;
		return;
	}

	hm_inorder(root->right, leaf_symbols, leaf_cnt);
}

static huffman_tree *construct_flattened(int *leaf_symbols, int st, int end)
{
	if (end - st <= 1)
		return hm_node_create(leaf_symbols[st], 0);
	int diff = end - st - 1;
	int fl = f_log2(diff);
	huffman_tree *inter = hm_node_create(0, 0);
	int bound = end - (1 << fl);
	inter->left = construct_flattened(leaf_symbols, st, bound);
	inter->right = construct_flattened(leaf_symbols, bound, end);

	return inter;
}

static void flatten(huffman_tree **p_root)
{
	int leaf_symbols[64] = {0};
	int leaf_cnt = 0;

	hm_inorder(*p_root, leaf_symbols, &leaf_cnt);

	hm_tree_destroy(*p_root);

	*p_root = construct_flattened(leaf_symbols, 0, leaf_cnt);
}

static void flatten_tree(huffman_tree **p_root, int maxlen)
{
	while (1) {
		int leaf_right = hm_count_leaves((*p_root)->right);

		if (leaf_right > (1 << (maxlen - 1))) {
			flatten(p_root);
			return;
		}

		maxlen--;
		p_root = &(*p_root)->right;
	}
}

static huffman_tree *hm_node_create(int value, int weight)
{
	huffman_tree *node = (huffman_tree *)calloc(1, sizeof(huffman_tree));
	assert(node);
	node->weight = weight;
	node->value = value;
	return node;
}

void hm_tree_destroy(huffman_tree *root)
{
	if (!root)
		return;

	hm_tree_destroy(root->left);
	hm_tree_destroy(root->right);

	free(root);
}

static void hm_codelengths(huffman_tree *root, int *codelengths, int curr)
{
	if (!root)
		return;

	if (HUFFMAN_IS_LEAF(root)) {
		codelengths[root->value] = curr;
		return;
	}

	hm_codelengths(root->left, codelengths, curr + 1);
	hm_codelengths(root->right, codelengths, curr + 1);
}

void hm_get_codelengths(int *weights, int *codelengths, int n, int maxlen)
{
	memset(codelengths, 0, (size_t)n * sizeof(int));

	z_heap *zh = zheap_create();

	for (int i = 0; i < n; i++) {
		if (weights[i] != 0) {
			huffman_tree *node = hm_node_create(i, weights[i]);
			zheap_push(zh, node, node->weight);
		}
	}

	if (zheap_is_empty(zh)) {
		fputs("HUFFMAN Error: all weights are zero.\n", stderr);
		zheap_destroy(zh);
		return;
	}

	if (zheap_get_size(zh) == 1) {
		huffman_tree *node = zheap_peek(zh);
		zheap_pop(zh);
		codelengths[node->value] = 1;
		free(node);
		zheap_destroy(zh);
		return;
	}

	huffman_tree *first, *second;
	
	while (zheap_get_size(zh) > 1) {
		first = zheap_peek(zh);
		zheap_pop(zh);

		second = zheap_peek(zh);
		zheap_pop(zh);

		huffman_tree *new_root = hm_node_create(0, 0);
		huffman_tree *longer = (first->height > second->height) ?
			first : second;
		huffman_tree *shorter = (longer == first) ? second : first;
		new_root->left = shorter;
		new_root->right = longer;
		HUFFMAN_CALC_HEIGHT(new_root);
		HUFFMAN_CALC_WEIGHT(new_root);

		zheap_push(zh, new_root, new_root->weight);
	}

	huffman_tree *root = zheap_peek(zh);
	zheap_pop(zh);
	zheap_destroy(zh);

	int height = root->height;

	hm_codelengths(root, codelengths, 0);
	hm_tree_destroy(root);

	huffman_tuple *table = hm_create_table(codelengths, n);
	root = hm_create_canonical(table, n);

	if (maxlen != 0 && height > maxlen) {
		/* Truncate tree to desired max height */

		flatten_tree(&root, maxlen);
		hm_codelengths(root, codelengths, 0);
	}

	/* Get codelengths of tree, cleanup and return */
	free(table);
	hm_tree_destroy(root);
	return;
}

static int comp_hm_tuple(const void *a, const void *b)
{
	int rc = ((huffman_tuple *)a)->len - ((huffman_tuple *)b)->len;
	if (rc)
		return rc;
	return ((huffman_tuple *)a)->val - ((huffman_tuple *)b)->val;
}

static int tuple_sort_alphabetical(const void *a, const void *b)
{
	return ((huffman_tuple *)a)->val - ((huffman_tuple *)b)->val;
}

huffman_tuple *hm_create_table(int *codelengths, int n)
{
	huffman_tuple *table =
		(huffman_tuple *)calloc((size_t)n, sizeof(huffman_tuple));
	assert(table);
	for (int i = 0; i < n; i++) {
		table[i].val = i;
		table[i].len = codelengths[i];
	}

	qsort(table, (size_t)n, sizeof(huffman_tuple), comp_hm_tuple);

	int is_set_first = 0;
	for (int i = 0; i < n; i++) {
		if (table[i].len == 0)
			continue;

		if (!is_set_first) {
			is_set_first = 1;
			continue;
		}
		table[i].code = (table[i - 1].code + 1) << (table[i].len - table[i - 1].len);
	}

	qsort(table, (size_t)n, sizeof(huffman_tuple), tuple_sort_alphabetical);
	return table;
}

huffman_tree *hm_create_canonical(huffman_tuple *table, int n)
{
	huffman_tree *root = hm_node_create(0, 0);

	for (int i = 0; i < n; i++) {
		if (table[i].len == 0)
			continue;

		huffman_tree *temp = root;
		for (int j = table[i].len - 1; j > 0; j--) {
			int bit = (table[i].code >> j) & 1;
			if (bit == 0) {
				if (!temp->left)
					temp->left = hm_node_create(0, 0);
				temp = temp->left;
			} else {
				if (!temp->right)
					temp->right = hm_node_create(0, 0);
				temp = temp->right;
			}
		}
		int bit = table[i].code & 1;
		if (bit == 0) {
			temp->left = hm_node_create(table[i].val, 0);
		} else {
			temp->right = hm_node_create(table[i].val, 0);
		}
	}

	return root;
}

void hm_debug(huffman_tree *root, int indent)
{
	for (int i = 0; i < indent; i++)
		putc(' ', stdout);
	if (!root) {
		printf("NIL\n");
		return;
	}

	if (HUFFMAN_IS_LEAF(root)) {
		printf("%03d\n", root->value);
	} else {
		printf("INR\n");
	}
	if (!HUFFMAN_IS_LEAF(root)) {
		hm_debug(root->left, indent + 4);
		hm_debug(root->right, indent + 4);
	}
}
