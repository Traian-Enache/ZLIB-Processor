#include "lzssutils.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>


circ_buff_t *cb_create(size_t capacity)
{
	circ_buff_t *cb = (circ_buff_t *)calloc(1, sizeof(circ_buff_t));
	assert(cb);

	cb->capacity = capacity;

	cb->buffer = (unsigned char *)calloc(capacity, sizeof(char));
	assert(cb->buffer);

	return cb;
}

void cb_destroy(circ_buff_t *cb)
{
	free(cb->buffer);
	free(cb);
}

int cb_is_empty(circ_buff_t *cb)
{
	if (!cb || !cb->size)
		return 1;
	return 0;
}

int cb_is_full(circ_buff_t *cb)
{
	if (!cb)
		return 0;
	if (cb->size == cb->capacity)
		return 1;
	return 0;
}

size_t cb_get_size(circ_buff_t *cb)
{
	if (!cb)
		return 0;
	return cb->size;
}

void cb_push(circ_buff_t *cb, unsigned char val)
{
	cb->buffer[cb->write_idx] = val;
	if (++(cb->write_idx) == cb->capacity)
		cb->write_idx = 0;
	if (cb->size == cb->capacity) {
		if (++(cb->read_idx) == cb->capacity)
			cb->read_idx = 0;
	} else {
		cb->size++;
	}
}

unsigned char cb_pop(circ_buff_t *cb)
{
	if (cb->size == 0)
		return 0;
	unsigned char val = cb->buffer[cb->read_idx];
	if (++(cb->read_idx) == cb->capacity)
		cb->read_idx = 0;
	cb->size--;
	return val;
}

unsigned char cb_get(circ_buff_t *cb, int idx)
{
	size_t real_idx = cb->read_idx + (size_t)idx;
	if (real_idx >= cb->capacity)
		real_idx -= cb->capacity;
	return cb->buffer[real_idx];
}

unsigned char cb_get_from_back(circ_buff_t *cb, int idx)
{
	return cb_get(cb, (int)cb->size - idx - 1);
}

static lz_node_t *lzl_new_node(int val)
{
	lz_node_t *node = (lz_node_t *)calloc(1, sizeof(lz_node_t));
	assert(node);
	node->data = val;
	return node;
}

lz_list_t *lzl_create()
{
	lz_list_t *list = (lz_list_t *)calloc(1, sizeof(lz_list_t));
	assert(list);
	return list;
}

void lzl_insert(lz_list_t *list, int idx, int val)
{
	if (idx > list->size)
		idx = list->size;

	lz_node_t *node = lzl_new_node(val);

	if (!list->size) {
		list->head = node;
		list->tail = node;
	} else if (idx == 0) {
		node->next = list->head;
		list->head->prev = node;
		list->head = node;
	} else if (idx == list->size) {
		list->tail->next = node;
		node->prev = list->tail;
		list->tail = node;
	} else {
		lz_node_t *aux = list->head;
		for (int i = 1; i < idx; i++) {
			aux = aux->next;
		}
		node->next = aux->next;
		node->prev = aux;
		aux->next = node;
		node->next->prev = node;
	}
	list->size++;
}

int lzl_extract(lz_list_t *list, int idx)
{
	if (list->size == 0) {
		return 0;
	} else if (idx >= list->size) {
		idx = list->size - 1;
	}
	int ret;
	if (list->size == 1) {
		ret = list->head->data;
		free(list->head);
		list->head = NULL;
		list->tail = NULL;
	} else if (idx == 0) {
		ret = list->head->data;
		lz_node_t *aux = list->head;
		list->head = aux->next;
		free(aux);
		list->head->prev = NULL;
	} else if (idx == list->size - 1) {
		ret = list->tail->data;
		lz_node_t *aux = list->tail->prev;
		aux->next = NULL;
		free(list->tail);
		list->tail = aux;
	} else {
		lz_node_t *aux = list->head;
		for (int i = 0; i < idx; i++) {
			aux = aux->next;
		}
		ret = aux->data;
		aux->prev->next = aux->next;
		aux->next->prev = aux->prev;
		free(aux);
	}
	list->size--;
	return ret;
}

int lzl_get(lz_list_t *list, int idx)
{
	if (list->size == 0)
		return 0;
	if (idx >= list->size)
		idx = list->size - 1;
	lz_node_t *aux = list->head;
	for (int i = 0; i < idx; i++) {
		aux = aux->next;
	}
	return aux->data;
}

void lzl_push_front(lz_list_t *list, int val)
{
	lzl_insert(list, 0, val);
}

void lzl_push_back(lz_list_t *list, int val)
{
	lzl_insert(list, list->size, val);
}

int lzl_pop_front(lz_list_t *list)
{
	return lzl_extract(list, 0);
}

int lzl_pop_back(lz_list_t *list)
{
	return lzl_extract(list, list->size - 1);
}

void lzl_clear(lz_list_t *list)
{
	while (list->size) {
		lzl_pop_front(list);
	}
}

void lzl_destroy(lz_list_t *list)
{
	lzl_clear(list);
	free(list);
}

backlink_array_t *bl_arr_create()
{
	backlink_array_t *arr = (backlink_array_t *)calloc(1, sizeof(*arr));
	assert(arr);

	arr->capacity = BL_ARR_DEFAULT_CAPACITY;
	arr->pos = (int *)calloc(arr->capacity, sizeof(int));
	arr->len = (unsigned char *)calloc(arr->capacity, sizeof(char));
	arr->dist = (unsigned short *)calloc(arr->capacity, sizeof(short));

	assert(arr->pos && arr->len && arr->dist);

	return arr;
}

void bl_arr_destroy(backlink_array_t *arr)
{
	free(arr->pos);
	free(arr->len);
	free(arr->dist);
	free(arr);
}

void bl_arr_expand(backlink_array_t *arr)
{
	if (arr->size == arr->capacity) {
		arr->capacity *= 2;
		arr->len = (unsigned char *)realloc(arr->len,
						arr->capacity * sizeof(char));
		arr->dist = (unsigned short *)realloc(arr->dist,
						arr->capacity * sizeof(short));
		arr->pos = (int *)realloc(arr->pos, arr->capacity * sizeof(int));

		assert(arr->len && arr->dist && arr->pos);
	}
}

void bl_arr_push(backlink_array_t *arr, int pos, int dist, int len)
{
	if (arr->size == arr->capacity)
		bl_arr_expand(arr);
	size_t index = arr->size;
	arr->len[index] = (unsigned char)((len - 3) & 0xFF);
	arr->dist[index] = (unsigned short)((dist - 1) & 0xFFFF);
	arr->pos[index] = pos;
	arr->size++;
}

int bl_arr_get(backlink_array_t *arr, int idx, int *pos, int *dist, int *len)
{
	if ((size_t)idx >= arr->size || idx < 0) {
		*pos = -1;
		return -1;
	}
	if (pos)
		*pos = arr->pos[idx];
	if (dist)
		*dist = arr->dist[idx] + 1;
	if (len)
		*len = arr->len[idx] + 3;

	return 0;
}

void bl_arr_reset(backlink_array_t *arr)
{
	arr->size = 0;
}

size_t bl_arr_size(backlink_array_t *arr)
{
	if (!arr)
		return 0;
	return arr->size;
}
