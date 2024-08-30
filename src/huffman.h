#ifndef _HUFFMAN_H
#define _HUFFMAN_H

typedef struct huffman_tree huffman_tree;
struct huffman_tree
{
	huffman_tree *left, *right;
	int value, weight, height;
};

typedef struct {
	int val, len, code;
} huffman_tuple;

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif  // MAX

#define HUFFMAN_IS_LEAF(node) (((node)->left || (node)->right) ? 0 : 1)
#define HUFFMAN_CALC_WEIGHT(node) {\
	if (!HUFFMAN_IS_LEAF(node))\
		node->weight = node->left->weight + node->right->weight;\
	}
#define HUFFMAN_CALC_HEIGHT(node) (\
	(node)->height = (HUFFMAN_IS_LEAF(node) ? 0 :\
	1 + MAX((node)->left->height, (node)->right->height)))

void hm_tree_destroy(huffman_tree *root);

void hm_get_codelengths(int *weights, int *codelengths, int n, int maxlen);

huffman_tuple *hm_create_table(int *codelengths, int n);

huffman_tree *hm_create_canonical(huffman_tuple *table, int n);

void hm_debug(huffman_tree *root, int indent);

#endif  // _HUFFMAN_H
