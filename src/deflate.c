#include "deflate.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "zutils.h"

#define MAX_MARKS 256

static inline __attribute__((__always_inline__)) int f_log2(int x)
{
	int res = -1;
	while (x) {
		x >>= 1;
		res++;
	}
	return res;
}

#define _GET_DIST_CODE(dist) ((dist) < 5 ? (dist) - 1 :\
	2 * f_log2((dist) - 1) + (1 & (((dist) - 1) >> f_log2(((dist) - 1) >> 1))))

#define _GET_LEN_CODE(len) ((len) == 258 ? 285 :\
	(len) < 11 ? (len) + 254 :\
	253 + 4 * f_log2((len) - 3) +\
	(3 & (((len) - 3) >> f_log2(((len) - 3) >> 2))))

#define _MIN(a, b) ((a) < (b) ? (a) : (b))

static int deflate_block(z_stream_t *strm);

static huffman_tuple *gen_clen_codes();

static int __fetch_data(z_stream_t *strm);

static int __dump_output(z_stream_t *strm);

static int safe_write_lsbf(z_stream_t *strm, int data, int nbits);

static int safe_write_msbf(z_stream_t *strm, int data, int nbits);

static int safe_write_byte(z_stream_t *strm, z_byte byte);

static int write_int_be(z_stream_t *strm, int val);

static int get_successive_val_count(int *arr, int idx, int n);

int deflate(FILE *src, FILE *dest)
{
	/* Init stream and LUTs */

	z_stream_t strm;
	zlib_init(&strm, src, dest, Z_MODE_DEFLATE);
	luts_init();

	/* Assign output stream to bitwise processor */

	bws_assign_stream(strm.bws, strm.out, CHUNK_SIZE);

	/* Write zlib header (default compression, 32K window size, no dict) */

	(void)bws_write_lsbf(strm.bws, 0x78, 8);
	(void)bws_write_lsbf(strm.bws, 0x9C, 8);

	/* Init sliding window */

	strm.sliding_window = cb_create(32768);		/* 32K */

	/* Process all input */

	int res = 0;

	while (strm.eof == 0) {
		res = deflate_block(&strm);

		if (res != Z_OK)
			break;
	}

	if (res != Z_OK && res != ZLIB_LAST_BLOCK_PROCESSED) {
		/* Perform cleanup and bail */

		zlib_destroy(&strm);
		return res;
	}

	/* Write adler checksum */

	res = write_int_be(&strm, (int)strm.adler);
	if (res != Z_OK) {
		/* Perform cleanup and bail */

		zlib_destroy(&strm);
		return res;
	}

	/* Dump remaining output */

	if (BW_USED_BYTES(strm.bws) != 0) {
		__dump_output(&strm);
	}

	zlib_destroy(&strm);
	return Z_OK;
}

static void rm_oldest_pos(z_stream_t *strm);

static void mark_curr_pos(z_stream_t *strm, int pos)
{
	int removed = 0;
	int ht_idx = 0;
	if (pos <= strm->avail_in - 2) {
		z_byte c1 = strm->in[pos];
		z_byte c2 = strm->in[pos + 1];
		ht_idx = (c1 << 8) + c2;
		lzl_push_front(&strm->hashtable[ht_idx], strm->total_out);

		if ((&strm->hashtable[ht_idx])->size > MAX_MARKS) {
			(void)lzl_pop_back(&strm->hashtable[ht_idx]);
			removed = 1;
		}
	}
	z_byte c1 = cb_get(strm->sliding_window, 0);
	z_byte c2 = cb_get(strm->sliding_window, 1);

	if (ht_idx != (c1 << 8) + c2 || removed == 0)
		rm_oldest_pos(strm);
}

static void rm_oldest_pos(z_stream_t *strm)
{
	if (cb_is_full(strm->sliding_window)) {
		z_byte c1 = cb_get(strm->sliding_window, 0);
		z_byte c2 = cb_get(strm->sliding_window, 1);
		
		int ht_idx = (c1 << 8) + c2;
		(void)lzl_pop_back(&strm->hashtable[ht_idx]);
	}
}

static int find_match(z_stream_t *strm, int pos, int *len, int *dist)
{
	int max_len = 2;

	*len = 0;
	*dist = 0;

	circ_buff_t *win = strm->sliding_window;

	if (pos < strm->avail_in - 2) {
		/* Look for matches only if there are enough bytes in input buffer */

		z_byte c1 = strm->in[pos];
		z_byte c2 = strm->in[pos + 1];

		int ht_idx = (c1 << 8) + c2;
		lz_list_t *list = &strm->hashtable[ht_idx];
		int mark_no = 0;

		for (lz_node_t *node = list->head; node; node = node->next) {
			/* Localize potential match */

			int global_pos = node->data;
			int distance = strm->total_out - global_pos;
			int i = 0;

			/* Calculate satisfactory length heuristic */

			int heuristic = mark_no - MAX_MARKS;	// Assume MAX_MARKS == 256
			heuristic *= heuristic * 7;
			heuristic >>= 11;
			heuristic += 32;

			/* Calculate match length */

			while (i < strm->avail_in - pos) {
				/* indexing in sliding window is done with dist - 1 */

				z_byte next_in_stream = strm->in[pos + i];
				int win_idx = distance - 1 - i;
				z_byte next_in_win;

				if (win_idx >= 0) {
					next_in_win = cb_get_from_back(win, win_idx);
				} else {
					next_in_win = strm->in[pos - win_idx - 1];
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
		if (*len >= 4) {
			return 1;
		}
	} else {
		return 0;
	}

	return 0;
}

static int deflate_block(z_stream_t *strm)
{
	/* Clear history of back-pointers */

	bl_arr_reset(strm->bl_arr);

	/* Fetch block of input data */

	int err = __fetch_data(strm);

	if (err != Z_OK)
		return err;

	/* Construct and write deflate block header */

	int is_last = strm->eof;
	int btype = (strm->avail_in > Z_DYN_TRESHOLD) ? 
		DEFLATE_BTYPE_DYN : DEFLATE_BTYPE_FIX;

	int header = (is_last & 1) | ((btype & BTYPE_MASK) << BTYPE_OFFSET);

	err = safe_write_lsbf(strm, header, DEFLATE_HEADER_SIZE);

	if (err != Z_OK)
		return err;

	int pos = 0;

	int lit_freq[MAX_LITLEN_CODES] = {0};
	lit_freq[256] = 1;

	int dist_freq[MAX_DIST_CODES] = {0};

	// find match;
	// if match found
	//		store match
	//		for 0 <= i < len
	//			mark entry in ht
	//			push char to sliding window
	//			advance pos and total_out
	// else
	//		mark entry in ht
	//		push char to window
	//		advance pos and total_out

	while (pos < strm->avail_in) {
		int dist = 0, len = 0;
		if (find_match(strm, pos, &len, &dist)) {
			/* Add duplicate to back-link array */

			bl_arr_push(strm->bl_arr, pos, dist, len);

			lit_freq[_GET_LEN_CODE(len)]++;
			dist_freq[_GET_DIST_CODE(dist)]++;

			for (int i = 0; i < len; i++) {
				/* Push LEN bytes to sliding_window */

				mark_curr_pos(strm, pos);
				cb_push(strm->sliding_window, strm->in[pos]);
				pos++;
				strm->total_out++;
			}
		} else {
			/* Push one byte to sliding window */

			mark_curr_pos(strm, pos);
			cb_push(strm->sliding_window, strm->in[pos]);

			lit_freq[strm->in[pos]]++;

			pos++;
			strm->total_out++;
		}
	}

	huffman_tuple *lit_table = NULL;
	huffman_tuple *dist_table = NULL;

	if (btype == DEFLATE_BTYPE_DYN || btype == DEFLATE_BTYPE_FIX) {
		/* Calculate literal code count and distance code count */

		int lit_cnt = 0;
		int dist_cnt = 0;

		for (int i = MAX_LITLEN_CODES; i >= 257; i--) {
			lit_cnt = i;
			if (lit_freq[i - 1])
				break;
		}
		for (int i = MAX_DIST_CODES; i >= 1; i--) {
			dist_cnt = i;
			if (dist_freq[i - 1])
				break;
		}
		int lit_clens[MAX_LITLEN_CODES] = {0};
		int dist_clens[MAX_DIST_CODES] = {0};

		/* Get codelengths for literal-length and distance sets */

		hm_get_codelengths(lit_freq, lit_clens, lit_cnt, 15);
		hm_get_codelengths(dist_freq, dist_clens, dist_cnt, 15);

		lit_table = hm_create_table(lit_clens, lit_cnt);
		dist_table = hm_create_table(dist_clens, dist_cnt);

		/* Merge codelengths into a single alphabet */

		int all_clens[MAX_TOTAL_CODES] = {0};
		memcpy(all_clens, lit_clens, (unsigned)lit_cnt * sizeof(int));
		memcpy(all_clens + lit_cnt, dist_clens,
			(unsigned)dist_cnt * sizeof(int));

		/* Run-length encode codelengths */

		int run_len_enc_clens[MAX_TOTAL_CODES] = {0};
		int extra_bits[MAX_TOTAL_CODES] = {0};
		int clen_freq[MAX_ALPHABET_CODES] = {0};

		int all_cnt = dist_cnt + lit_cnt;

		int p1 = 0, p2 = 0;

		while (p1 < all_cnt) {
			if (all_clens[p1]) {
				/* Current codelength != 0 */

				int rpt = get_successive_val_count(all_clens, p1, all_cnt) - 1;
				clen_freq[all_clens[p1]]++;
				run_len_enc_clens[p2++] = all_clens[p1++];

				if (rpt >= 3) {
					int repeat_cnt = _MIN(rpt, 6);
					run_len_enc_clens[p2] = 16;
					extra_bits[p2++] = repeat_cnt - 3;
					p1 += repeat_cnt;
					clen_freq[16]++;
				}
			} else {
				/* Current codelength == 0 */

				int rpt = get_successive_val_count(all_clens, p1, all_cnt);
				
				if (rpt < 3) {
					clen_freq[0]++;
					run_len_enc_clens[p2++] = 0;
					p1++;
				} else {
					int clen_code = (rpt > 10) ? 18 : 17;
					int repeat_cnt = _MIN(rpt, 138);

					clen_freq[clen_code]++;
					run_len_enc_clens[p2] = clen_code;
					extra_bits[p2++] = repeat_cnt - (clen_code == 17 ? 3 : 11);
					p1 += repeat_cnt;
				}
			}
		}

		// int order[MAX_ALPHABET_CODES] = 
		// 	{16, 17, 18, 0, 8, 7,
		// 	9, 6, 10, 5, 11, 4,
		// 	12, 3, 13, 2, 14, 1, 15};

		int clen_cnt = 19;

		huffman_tuple *clen_table = gen_clen_codes();

		int hlit = lit_cnt - 257;
		int hdist = dist_cnt - 1;
		int hclen = clen_cnt - 4;

		/* Write HLIT, HDIST and HCLEN */

		if ((err = safe_write_lsbf(strm, hlit, HLIT_BITS) != Z_OK))
			return err;
		if ((err = safe_write_lsbf(strm, hdist, HDIST_BITS) != Z_OK))
			return err;
		if ((err = safe_write_lsbf(strm, hclen, HCLEN_BITS) != Z_OK))
			return err;

		/* Write codelengths for run-length encoded alphabet */

		for (int i = 0; i < clen_cnt; i++) {
			if (i < 3) {
				if ((err = safe_write_lsbf(strm, 3, 3)) != Z_OK)
					return err;
			} else if (i < 7) {
				if ((err = safe_write_lsbf(strm, 4, 3)) != Z_OK)
					return err;
			} else {
				if ((err = safe_write_lsbf(strm, 5, 3)) != Z_OK)
					return err;
			}
		}

		/* Write run-length encoded codelengths for merged alphabets */
		
		for (int i = 0; i < p2; i++) {
			/* Write next codelength */
			err = safe_write_msbf(strm, clen_table[run_len_enc_clens[i]].code,
				clen_table[run_len_enc_clens[i]].len);

			if (err != Z_OK) {
				free(clen_table);
				return err;
			}

			if (run_len_enc_clens[i] >= 16) {
				/* Write extra bits */

				int nbits = run_len_enc_clens[i] == 16 ? 2 :
					run_len_enc_clens[i] == 17 ? 3 : 7;

				err = safe_write_lsbf(strm, extra_bits[i], nbits);

				if (err != Z_OK) {
					free(clen_table);
					return err;
				}
			}
		}

		free(clen_table);
	} else if (btype == DEFLATE_BTYPE_FIX) {
		/* Generate fixed width huffman tables */

		int lit_clens[MAX_LITLEN_CODES] = {0};
		int dist_clens[MAX_DIST_CODES] = {0};

		for (int i = 0; i < MAX_DIST_CODES; i++)
			dist_clens[i] = 5;

		for (int i = 0; i <= 143; i++)
			lit_clens[i] = 8;

		for (int i = 144; i <= 255; i++)
			lit_clens[i] = 9;

		for (int i = 256; i <= 279; i++)
			lit_clens[i] = 7;

		for (int i = 280; i <= 287; i++)
			lit_clens[i] = 8;

		lit_table = hm_create_table(lit_clens, MAX_LITLEN_CODES);
		dist_table = hm_create_table(dist_clens, MAX_DIST_CODES);
	}

	/* Write symbols */
	pos = 0;
	int match_no = 0;

	int match_pos;
	int match_len;
	int match_dist;

	bl_arr_get(strm->bl_arr, match_no, &match_pos, &match_dist, &match_len);

	while (pos < strm->avail_in) {
		if (pos == match_pos) {
			/* Write back-pointer, increment POS by LEN and get next match */
			int len_code = _GET_LEN_CODE(match_len);
			int len_nbits = LIT_EXTRA_BITS(len_code);
			int len_extra = match_len - LEN_BASE_VAL(len_code);

			/* Write huffman-encoded length code */

			err = safe_write_msbf(strm, lit_table[len_code].code,
				lit_table[len_code].len);
			
			if (err != Z_OK) {
				free(lit_table);
				free(dist_table);

				return err;
			}

			/* Write extra bits for length */

			err = safe_write_lsbf(strm, len_extra, len_nbits);

			if (err != Z_OK) {
				free(lit_table);
				free(dist_table);

				return err;
			}

			int dist_code = _GET_DIST_CODE(match_dist);
			int dist_nbits = DIST_EXTRA_BITS(dist_code);
			int dist_extra = match_dist - DIST_BASE_VAL(dist_code);

			/* Write huffman-encoded distance code */

			err = safe_write_msbf(strm, dist_table[dist_code].code,
				dist_table[dist_code].len);

			if (err != Z_OK) {
				free(lit_table);
				free(dist_table);

				return err;
			}

			/* Write extra bits for distance */

			err = safe_write_lsbf(strm, dist_extra, dist_nbits);

			if (err != Z_OK) {
				free(lit_table);
				free(dist_table);

				return err;
			}

			pos += match_len;
			(void)bl_arr_get(strm->bl_arr, ++match_no,
				&match_pos, &match_dist, &match_len);
		} else {
			z_byte lit = strm->in[pos];
			/* Write huffman-encoded literal nad increment POS by 1 */

			err = safe_write_msbf(strm, lit_table[lit].code, lit_table[lit].len);

			if (err != Z_OK) {
				free(lit_table);
				free(dist_table);

				return err;
			}
			pos++;
		}
	}


	/* Write end of block */
	err = safe_write_msbf(strm, lit_table[256].code, lit_table[256].len);

	if (err != Z_OK) {
		free(lit_table);
		free(dist_table);

		return err;
	}

	free(lit_table);
	free(dist_table);
	update_adler(&strm->adler, strm->in, strm->avail_in);
	return Z_OK;
}

static int __fetch_data(z_stream_t *strm)
{
	strm->avail_in = (int)fread(strm->in, 1, CHUNK_SIZE, strm->src);

	if (ferror(strm->src))
		return FILE_ERROR;

	strm->eof = Z_EOF(strm->src);

	return Z_OK;
}

static int __dump_output(z_stream_t *strm)
{
	(void)fwrite(strm->out, 1, BW_USED_BYTES(strm->bws), strm->dest);

	if (ferror(strm->dest))
		return FILE_ERROR;

	(void)memset(strm->out, 0, CHUNK_SIZE);
	(void)bws_assign_stream(strm->bws, strm->out, CHUNK_SIZE);

	return Z_OK;
}

static int safe_write_lsbf(z_stream_t *strm, int data, int nbits)
{
	int res = bws_write_lsbf(strm->bws, data, nbits);

	if (res) {
		int err = __dump_output(strm);
		if (err != Z_OK)
			return err;
		(void)bws_write_lsbf(strm->bws, data >> (nbits - res), res);
	}

	return Z_OK;
}

static int safe_write_msbf(z_stream_t *strm, int data, int nbits)
{
	int res = bws_write_msbf(strm->bws, data, nbits);

	if (res) {
		int err = __dump_output(strm);
		if (err != Z_OK)
			return err;
		(void)bws_write_msbf(strm->bws, data, res);
	}

	return Z_OK;
}

static int safe_write_byte(z_stream_t *strm, z_byte byte)
{
	bws_flush(strm->bws);

	if (bwsEOS(strm->bws)) {
		int err = __dump_output(strm);
		if (err != Z_OK)
			return err;
	}

	strm->bws->stream[BW_BYTENO(strm->bws)] = byte;

	strm->bws->idx += 8;

	return Z_OK;
}

static int write_int_be(z_stream_t *strm, int val)
{
	for (int i = 3; i >= 0; i--) {
		z_byte byte = ((unsigned)val >> (8 * i)) & 0xff;

		int err = safe_write_byte(strm, byte);

		if (err != Z_OK)
			return err;
	}

	return Z_OK;
}

static int get_successive_val_count(int *arr, int idx, int n)
{
    int count = 1;

    while (idx < n - 1) {
        if (arr[idx] == arr[idx + 1])
            count++;
        else
            break;
        idx++;
    }

    return count;
}

static huffman_tuple *gen_clen_codes()
{
	int order[MAX_ALPHABET_CODES] =
		{16, 17, 18, 0, 8, 7,
		9, 6, 10, 5, 11, 4,
		12, 3, 13, 2, 14, 1, 15};
	int codelengths[MAX_ALPHABET_CODES] = {0};

	for (int i = 0; i < 3; i++) {
		codelengths[order[i]] = 3;
	}
	for (int i = 3; i < 7; i++) {
		codelengths[order[i]] = 4;
	}
	for (int i = 7; i < MAX_ALPHABET_CODES; i++) {
		codelengths[order[i]] = 5;
	}

	return hm_create_table(codelengths, MAX_ALPHABET_CODES);
}
