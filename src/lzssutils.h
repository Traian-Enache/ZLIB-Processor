#ifndef _LZSSUTILS_H
#define _LZSSUTILS_H

#include <stddef.h>

#define FLOOR(x) ((int)(x) - ((x) < 0))
#define SGN(x) ((((x) >= 0) << 1) - 1)
#define DMOD(x, y) ((x) - (y) * FLOOR((double)(x) / (y)))	// double mod
#define IMOD(x, y) ((((x) % (y)) < 0) * (y) + ((x) % (y)))	// int mod

typedef struct circ_buff_t {
	unsigned char *buffer;
	size_t capacity, size;
	size_t read_idx, write_idx;
} circ_buff_t;

circ_buff_t *cb_create(size_t capacity);

void cb_destroy(circ_buff_t *cb);

int cb_is_empty(circ_buff_t *cb);

int cb_is_full(circ_buff_t *cb);

size_t cb_get_size(circ_buff_t *cb);

void cb_push(circ_buff_t *cb, unsigned char val);

unsigned char cb_pop(circ_buff_t *cb);

unsigned char cb_get(circ_buff_t *cb, int idx);

unsigned char cb_get_from_back(circ_buff_t *cb, int idx);

typedef struct lz_node_t {
	struct lz_node_t *next, *prev;
	int data;
} lz_node_t;

typedef struct lz_list_t {
	lz_node_t *head, *tail;
	int size;
} lz_list_t;

lz_list_t *lzl_create();

void lzl_clear(lz_list_t *list);

void lzl_destroy(lz_list_t *list);

void lzl_insert(lz_list_t *list, int idx, int val);

int lzl_extract(lz_list_t *list, int idx);

int lzl_get(lz_list_t *list, int idx);

void lzl_push_front(lz_list_t *list, int val);

void lzl_push_back(lz_list_t *list, int val);

int lzl_pop_front(lz_list_t *list);

int lzl_pop_back(lz_list_t *list);

#define BL_ARR_DEFAULT_CAPACITY 32

typedef struct backlink_array_t {
	size_t size, capacity;
	int *pos;
	unsigned short *dist;
	unsigned char *len;
} backlink_array_t;

backlink_array_t *bl_arr_create();

void bl_arr_destroy(backlink_array_t *arr);

void bl_arr_expand(backlink_array_t *arr);

void bl_arr_push(backlink_array_t *arr, int pos, int dist, int len);

int bl_arr_get(backlink_array_t *arr, int idx, int *pos, int *dist, int *len);

void bl_arr_reset(backlink_array_t *arr);

size_t bl_arr_size(backlink_array_t *arr);

#endif  // _LZSSUTILS_H
