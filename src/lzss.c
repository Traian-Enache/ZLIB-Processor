#include "lzss.h"
#include "lzssutils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define WINDOW_SIZE 32768
#define MAX_MATCH_LEN 258
#define MAX_MARKS 256		// maximum recorded potential match locations
							// per lookup table entry

static int find_match(circ_buff_t *window, circ_buff_t *lookahead_buf,
	lz_list_t *dict_table, int pos, int *dist, int *len);

static void advance(circ_buff_t *window, circ_buff_t *lookahead_buf,
	lz_list_t *dict_table, int *pos, FILE *out);

static void add_entry(lz_list_t *dict_table, circ_buff_t *lookahead_buf,
	circ_buff_t *window, int pos);

void lzss_compress(FILE *in, FILE *out)
{
	// set up data structures
	circ_buff_t *window = cb_create(WINDOW_SIZE);
	backlink_array_t *matches = bl_arr_create();
	lz_list_t dict_lookuptable[65536];
	memset(dict_lookuptable, 0, 65536 * sizeof(lz_list_t));
	circ_buff_t *lookahead_buffer = cb_create(MAX_MATCH_LEN);
	
	// load look-ahead buffer
	for (int i = 0; i < MAX_MATCH_LEN; i++) {
		int next_byte = fgetc(in);
		if (next_byte != EOF) {
			cb_push(lookahead_buffer, (unsigned char)next_byte);
		} else {
			break;
		}
	}

	// find matches

	int pos = 0;

	while (cb_get_size(lookahead_buffer) > 2) {
		// look for matches if there is enough input in the lookahead buffer
		
		int len = 0, dist = 0;
		if (find_match(window, lookahead_buffer, dict_lookuptable,
		pos, &dist, &len)) {
			bl_arr_push(matches, pos, dist, len);
			for (int i = 0; i < len; i++) {
				advance(window, lookahead_buffer, dict_lookuptable,
				&pos, in);
			}
		} else {
			advance(window, lookahead_buffer, dict_lookuptable,
				&pos, in);
		}
	}

	rewind(in);
	pos = 0;

	// destroy utilitary structures and write compressed data
	cb_destroy(window);
	cb_destroy(lookahead_buffer);
	for (int i = 0; i < 65536; i++) {
		lzl_clear(&dict_lookuptable[i]);
	}
	uint8_t outbuf[25] = {0};
	int buffered_symbols = 0;
	int buf_idx = 1;
	int match_no = 0;
	int match_pos, match_len, match_dist;
	bl_arr_get(matches, match_no, &match_pos, &match_dist, &match_len);

	while (1) {
		if (match_pos == pos) {
			// output match to buffer in format DDDDDDDD DDDDDDDD LLLLLLLL
			// little endian
			*(uint16_t *)(outbuf + buf_idx) = (uint16_t)(match_dist - 1);
			outbuf[buf_idx + 2] = (uint8_t)(match_len - 3);
			buf_idx += 3;

			// advance in input file
			fseek(in, match_len, SEEK_CUR);
			pos += match_len;

			// load next match
			bl_arr_get(matches, ++match_no,
				&match_pos, &match_dist, &match_len);
		} else {
			pos++;
			int next_byte = fgetc(in);
			if (next_byte != EOF) {
				outbuf[0] |= (uint8_t)(1 << buffered_symbols);
				outbuf[buf_idx++] = (uint8_t)next_byte;
			} else {
				break;
			}
		}
		if (++buffered_symbols == 8) {
			// write buffer to file and reset buffer
			(void)fwrite(outbuf, 1u, (size_t)buf_idx, out);
			buf_idx = 1;
			memset(outbuf, 0, 25);
			buffered_symbols = 0;
		}
	}

	// flush remaining data from buffer to output
	if (buffered_symbols) {
		(void)fwrite(outbuf, 1u, (size_t)buf_idx, out);
	}

	// perform cleanup
	bl_arr_destroy(matches);
}

// decompresses block of 8 symbols and writes data to output.
// returns 1 if block has less than 8 symbols, 0 otherwise
static int decompress_block(circ_buff_t *window, int symbol_identifier,
	uint8_t *block_buffer, int block_size, FILE *out)
{
	int block_end = 0;
	int block_idx = 0;
	size_t buf_idx = 0;
	uint8_t out_buffer[258 << 3] = {0};
	for (int i = 0; i < 8; i++) {
		if (block_idx >= block_size) {
			block_end = 1;
			break;
		}
		if ((symbol_identifier >> i) & 1) {
			// next symbol is a literal byte
			uint8_t next_byte = block_buffer[block_idx++];
			out_buffer[buf_idx++] = next_byte;
			cb_push(window, next_byte);
		} else {
			int dist_m1 = *(unsigned short *)(block_buffer + block_idx);
			int len = block_buffer[block_idx + 2] + 3;
			block_idx += 3;

			// decode len bytes
			for (int i = 0; i < len; i++) {
				uint8_t next_byte = cb_get_from_back(window, dist_m1);
				out_buffer[buf_idx++] = next_byte;
				cb_push(window, next_byte);
			}
		}
	}

	// write decoded data to output
	(void)fwrite(out_buffer, 1, buf_idx, out);
	return block_end;
}

void lzss_decompress(FILE *in, FILE *out)
{
	// Initialize window
	circ_buff_t *window = cb_create(WINDOW_SIZE);

	int symbol_identifier;
	uint8_t block_buffer[24];
	int last_block = 0;

	// decompress data
	while (!last_block)
	{
		if ((symbol_identifier = fgetc(in)) != EOF) {
			// calculate block size
			size_t block_size = 0;
			size_t read_size;
			for (int i = 0; i < 8; i++) {
				block_size += (symbol_identifier >> i) & 1 ? 1 : 3;
			}
			if ((read_size = fread(block_buffer, 1, block_size, in))
				!= block_size) {
				last_block = 1;

				if (block_size == 0)
					break;
			}
			if (decompress_block(window, symbol_identifier, block_buffer,
				(int)read_size, out)) {
				break;
			}
		} else {
			break;
		}
	}
	
	cb_destroy(window);
}

// adds a potential match location in the dictionary lookup table and removes
// the oldest location in the dictionary, and eventually removes the oldest
// location in a particular dict entry, if there are too many marks
static void add_entry(lz_list_t *dict_table, circ_buff_t *lookahead_buf,
	circ_buff_t *window, int pos)
{
	int removed = 0;
	int dict_idx = 0;
	if (cb_get_size(lookahead_buf) > 2) {
		// marks current location
		uint8_t c1 = cb_get(lookahead_buf, 0);
		uint8_t c2 = cb_get(lookahead_buf, 1);
		dict_idx = (c1 << 8) + c2;
		lzl_push_front(&dict_table[dict_idx], pos);

		// if section of dictionary contains too many entries, remove oldest
		if ((&dict_table[dict_idx])->size > MAX_MARKS) {
			(void)lzl_pop_back(&dict_table[dict_idx]);
			removed = 1;
		}
	}

	// remove oldest entry (it becomes irrelevant as it gets outside of window)
	uint8_t c1 = cb_get(window, 0);
	uint8_t c2 = cb_get(window, 1);

	if ((dict_idx != (c1 << 8) + c2 || removed == 0) && cb_is_full(window)) {
		dict_idx = (c1 << 8) + c2;
		lzl_pop_back(&dict_table[dict_idx]);
	}
}

static void advance(circ_buff_t *window, circ_buff_t *lookahead_buf,
	lz_list_t *dict_table, int *pos, FILE *in)
{
	add_entry(dict_table, lookahead_buf, window, *pos);
	(*pos)++;
	cb_push(window, cb_pop(lookahead_buf));

	int next_byte = fgetc(in);
	if (next_byte != EOF) {
		cb_push(lookahead_buf, (uint8_t)next_byte);
	}
}

static int find_match(circ_buff_t *window, circ_buff_t *lookahead_buf,
	lz_list_t *dict_table, int pos, int *dist, int *len)
{
	int max_len = 2;

	*len = 0;
	*dist = 0; 

	uint8_t c1 = cb_get(lookahead_buf, 0);
	uint8_t c2 = cb_get(lookahead_buf, 1);

	int dict_idx = (c1 << 8) + c2;
	lz_list_t *list = &dict_table[dict_idx];
	int mark_no = 0;

	for (lz_node_t *node = list->head; node; node = node->next) {
		// Localize potential match

		int global_pos = node->data;
		int distance = pos - global_pos;
		int i = 0;

		// Calculate satisfactory length heuristic

		int heuristic = mark_no - MAX_MARKS;	// Assume MAX_MARKS == 256
		heuristic *= heuristic * 7;
		heuristic >>= 11;
		heuristic += 32;

		// Calculate match length

		while (i < (int)cb_get_size(lookahead_buf)) {
			// indexing in sliding window is done with dist - 1

			uint8_t next_in_stream = cb_get(lookahead_buf, i);
			int win_idx = distance - 1 - i;
			uint8_t next_in_win;

			if (win_idx >= 0) {
				next_in_win = cb_get_from_back(window, win_idx);
			} else {
				next_in_win = cb_get(lookahead_buf, -(win_idx + 1));
			}

			if (next_in_stream != next_in_win)
				break;

			if (i + 1 > max_len) {
				max_len = i + 1;
				*len = i + 1;
				*dist = distance;
			}
			if (*len == 258) {
				return 1;
			}
			i++;
		}
		if (*len >= heuristic) {
			return 1;
		}
		mark_no++;
	}
	if (*len >= 3) {
		return 1;
	}

	return 0;
}
