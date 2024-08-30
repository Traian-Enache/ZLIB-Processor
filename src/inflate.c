#include "inflate.h"
#include "zutils.h"
#include <stdlib.h>
#include <assert.h>

static int inflate_block(z_stream_t *strm);

static int read_input(z_stream_t *strm);

static void dump_output(z_stream_t *strm);

static int safe_read_lsbf(z_stream_t *strm, int *data, int nbits);

static int safe_read_byte(z_stream_t *strm, unsigned char *byte);

static int read_short_le(z_stream_t *strm, unsigned short *val);

static int read_short_be(z_stream_t *strm, unsigned short *val);

static int read_int_be(z_stream_t *strm, int *val);

int inflate(FILE *src, FILE *dest)
{
	/* Init z_stream and lookup tables */

	luts_init();

	z_stream_t strm, *p_zstrm;
	zlib_init(&strm, src, dest, Z_MODE_INFLATE);
	p_zstrm = &strm;

	/* Get first chunk of input */

	int res = 0;
	if ((res = read_input(&strm)) != Z_OK) {
		/* Perform cleanup and bail */

		zlib_destroy(p_zstrm);
		return res;
	}

	unsigned short zlib_header = 0;
	if ((res = read_short_be(p_zstrm, &zlib_header)) != Z_OK) {
		/* Perform cleanup and bail */

		zlib_destroy(p_zstrm);
		return res;
	}

	/* Check header for corruption */

	if (zlib_header % 31 != 0) {
		/* Perform cleanup and bail */

		zlib_destroy(p_zstrm);
		return CORRUPT_ZLIB_HEADER;
	}

	int cm = (zlib_header >> CM_OFFSET) & CM_MASK;
	if (cm != 8) {
		/* Perform cleanup and bail */

		zlib_destroy(p_zstrm);
		return INVALID_COMP_METHOD;
	}

	int cinfo = (zlib_header >> CINFO_OFFSET) & CINFO_MASK;
	if (cinfo >= 8) {
		/* Perform cleanup and bail */

		zlib_destroy(p_zstrm);
		return INVALID_WINDOW_SIZE;
	}

	/* Check for presence of dictionary */

	if ((zlib_header >> DICT_OFFSET) & DICT_MASK) {
		/* Perform cleanup and bail */

		zlib_destroy(p_zstrm);
		return DICT_IS_USED;
	}

	size_t window_size = 1u << (cinfo + 8);

	/* Initialize sliding window */

	p_zstrm->sliding_window = cb_create(window_size);
	int blk_res;

	while (1) {
		/* Process input blocks until encountering the end of stream or
		an exception */

		blk_res = inflate_block(p_zstrm);

		if (blk_res != Z_OK)
			break;
	}

	if (blk_res != ZLIB_LAST_BLOCK_PROCESSED) {
		/* Perform cleanup and bail */

		zlib_destroy(p_zstrm);
		return blk_res;
	}

	/* Dump remaining output */

	if (strm.avail_out != 0) {
		dump_output(&strm);
	}

	/* Validate Adler-32 checksum */

	bws_flush(p_zstrm->bws);

	unsigned int checksum;
	if ((res = read_int_be(p_zstrm, (int *)&checksum)) != Z_OK) {
		/* Perform cleanup and bail */

		zlib_destroy(p_zstrm);
		return res;
	}

	if (checksum != p_zstrm->adler) {
		zlib_destroy(p_zstrm);
		return ADLER_CHECKSUM_ERR;
	}

	zlib_destroy(&strm);

	return Z_OK;
}

static void push_lit_to_output(z_stream_t *strm, unsigned char lit)
{
	cb_push(strm->sliding_window, lit);

	if (strm->avail_out == CHUNK_SIZE)
		dump_output(strm);

	strm->out[strm->avail_out++] = lit;
}

static void gen_fixed_huffman_trees(huffman_tree **litlen_codes, 
	huffman_tree **dist_codes)
{
	/* Init default codelengths */

	int dist_clens[MAX_DIST_CODES] = {0};
	int litlen_clens[MAX_LITLEN_CODES] = {0};

	for (int i = 0; i < MAX_DIST_CODES; i++) {
		dist_clens[i] = 5;
	}
	for (int i = 0; i < MAX_LITLEN_CODES; i++) {
		if (i <= 143)
			litlen_clens[i] = 8;
		else if (i <= 255)
			litlen_clens[i] = 9;
		else if (i <= 279)
			litlen_clens[i] = 7;
		else
			litlen_clens[i] = 8;
	}

	/* Create (auxiliary) huffman tables */

	huffman_tuple *litlen_table, *dist_table;
	litlen_table = hm_create_table(litlen_clens, MAX_LITLEN_CODES);
	dist_table = hm_create_table(dist_clens, MAX_DIST_CODES);

	/* Generate fixed huffman trees */

	*litlen_codes = hm_create_canonical(litlen_table, MAX_LITLEN_CODES);
	*dist_codes = hm_create_canonical(dist_table, MAX_DIST_CODES);

	/* Destroy huffman tables */

	free(litlen_table);
	free(dist_table);
}

static int huffman_decode_next(z_stream_t *strm, huffman_tree *tree,
	int *val)
{
	int bit, res;
	huffman_tree *root = tree;

	while (!HUFFMAN_IS_LEAF(root)) {
		if ((res = safe_read_lsbf(strm, &bit, 1)) != Z_OK)
			return res;

		switch (bit) {
		case 0:
			root = root->left;
			break;
		case 1:
			root = root->right;
			break;
		}
	}
	
	*val = root->value;
	return Z_OK;
}

static int read_huffman_codes(z_stream_t *strm,
	huffman_tree **litlen_tree, huffman_tree **dist_tree)
{
	int hlit = 0;
	int hdist = 0;
	int hclen = 0;
	int res = 0;
	int temp = 0;

	/* Read number of literal codes, distance codes and alphabet codes */

	if ((res = safe_read_lsbf(strm, &hlit, HLIT_BITS)) != Z_OK)
		return res;
	if ((res = safe_read_lsbf(strm, &hdist, HDIST_BITS)) != Z_OK)
		return res;
	if ((res = safe_read_lsbf(strm, &hclen, HCLEN_BITS)) != Z_OK)
		return res;

	int lit_cnt = hlit + 257;
	int dist_cnt = hdist + 1;
	int clen_cnt = hclen + 4;

	/* Set order of alphabet codes */

	int alphabet_clens[MAX_ALPHABET_CODES] = {0};
	int alph_order[MAX_ALPHABET_CODES] = {16, 17, 18, 0, 8, 7, 9, 6, 10,
										5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

	/* Read HCLEN + 4 alphabet codes */

	for (int i = 0; i < clen_cnt; i++) {
		if ((res = safe_read_lsbf(strm, &temp, 3)) != Z_OK)
			return res;
		alphabet_clens[alph_order[i]] = temp;
	}
	/* Construct the codelength tree */

	huffman_tuple *clen_table = hm_create_table(alphabet_clens,
		MAX_ALPHABET_CODES);
	huffman_tree *clen_tree = hm_create_canonical(clen_table,
		MAX_ALPHABET_CODES);
	free(clen_table);

	int all_codelens[MAX_TOTAL_CODES] = {0};
	int pos = 0;
	while (pos < dist_cnt + lit_cnt) {
		if ((res = huffman_decode_next(strm, clen_tree, &temp)) != Z_OK) {
			/* Perform cleanup and bail */

			hm_tree_destroy(clen_tree);
			return res;
		}

		/* Parse all codelengths */

		if (temp <= 15) {
			all_codelens[pos] = temp;
			pos++;
		} else if (temp == 16) {
			/* Read extra bits and add to base value for symbol 16 */

			int nbits = CLEN_EXTRA_BITS(temp);
			int xtra = 0;

			if ((res = safe_read_lsbf(strm, &xtra, nbits)) != Z_OK) {
				hm_tree_destroy(clen_tree);
				return res;
			}

			xtra += 3;
			/* Parse previous codelength for XTRA amount of times */

			for (int i = 0; i < xtra; i++) {
				all_codelens[pos] = all_codelens[pos - 1];
				pos++;
			}
		} else if (temp == 17 || temp == 18) {
			/* Read extra bits value and add to base value */

			int nbits = CLEN_EXTRA_BITS(temp);
			int xtra = 0;
			if ((res = safe_read_lsbf(strm, &xtra, nbits)) != Z_OK) {
				hm_tree_destroy(clen_tree);
				return res;
			}
			xtra += (temp == 17 ? 3 : 11);
			/* Parse codelength 0 for XTRA amount of times */

			for (int i = 0; i < xtra; i++) {
				all_codelens[pos] = 0;
				pos++;
			}
		}
	}

	/* Destroy codelength tree */

	hm_tree_destroy(clen_tree);

	/* Create literal-length and distance trees using the codelengths decoded */

	huffman_tuple *lit_table = hm_create_table(all_codelens, lit_cnt);
	huffman_tuple *dist_table = hm_create_table(all_codelens + lit_cnt,
		dist_cnt);

	*litlen_tree = hm_create_canonical(lit_table, lit_cnt);
	*dist_tree = hm_create_canonical(dist_table, dist_cnt);

	free(lit_table);
	free(dist_table);

	return Z_OK;
}

static int parse_match(z_stream_t *strm, int code, huffman_tree *dist_tree)
{
	int res = Z_OK;
	int len_no_bits = LIT_EXTRA_BITS(code);
	int extra_len = 0;

	/* Read extra bits for len and calculate final length */

	if ((res = safe_read_lsbf(strm, &extra_len, len_no_bits)) != Z_OK)
		return res;

	int len = extra_len + LEN_BASE_VAL(code);

	/* Decode distance code */

	int dist_code = 0;
	if ((res = huffman_decode_next(strm, dist_tree, &dist_code)) != Z_OK)
		return res;

	/* Read extra bits for distance code */

	int dist_no_bits = DIST_EXTRA_BITS(dist_code);
	int extra_dist = 0;
	
	if ((res = safe_read_lsbf(strm, &extra_dist, dist_no_bits)) != Z_OK)
		return res;

	/* Calculate final distance */

	size_t dist = (size_t)(extra_dist + DIST_BASE_VAL(dist_code));
	if (dist > strm->sliding_window->size)
		return INVALID_MATCH_LEN;

	/* Push len characters at distance dist to output stream */

	for (int i = 0; i < len; i++) {
		size_t idx = strm->sliding_window->size - dist;

		/* Get char from desired position */
		z_byte lit = cb_get(strm->sliding_window, (int)idx);
		push_lit_to_output(strm, lit);
	}

	return Z_OK;
}

static int inflate_block(z_stream_t *strm)
{
	int read_result = 0;

	/* read header */

	int header = 0;
	read_result = safe_read_lsbf(strm, &header, DEFLATE_HEADER_SIZE);
	if (read_result != Z_OK)
		return read_result;

	int is_last = header & 1;
	int btype = (header >> BTYPE_OFFSET) & BTYPE_MASK;

	if (btype == DEFLATE_BTYPE_ERR) {
		return ILLEGAL_BTYPE;
	} else if (btype == DEFLATE_BTYPE_LIT) {
		/* Flush stream and read len and nlen */

		bws_flush(strm->bws);

		unsigned short len, nlen;
		if ((read_result = read_short_le(strm, &len)) != Z_OK)
			return read_result;
		if ((read_result = read_short_le(strm, &nlen)) != Z_OK)
			return read_result;

		/* Check LEN against NLEN */

		if ((len & 0xffff) != (~nlen & 0xffff))
			return LEN_CHECK_FAIL;

		/* Read literals and add to output block and to sliding window */

		unsigned char lit;
		for (int i = 0; i < (int)len; i++) {	
			if ((read_result = safe_read_byte(strm, &lit)) != Z_OK)
				return read_result;
			
			push_lit_to_output(strm, lit);
		}
	} else {
		huffman_tree *litlen_codes = NULL, *dist_codes = NULL;
		if (btype == DEFLATE_BTYPE_FIX) {
			/* Generate fixed huffman codes */

			gen_fixed_huffman_trees(&litlen_codes, &dist_codes);
		} else if (btype == DEFLATE_BTYPE_DYN) {
			/* Read and construct dynamic huffman codes */

			if (read_huffman_codes(strm, &litlen_codes, &dist_codes)
				!= Z_OK) {
				/* Perform cleanup and bail */

				hm_tree_destroy(litlen_codes);
				hm_tree_destroy(dist_codes);
				return STREAM_TOO_SHORT;
			}
		}
		/* Loop until end of block (256) reached */
		
		while (1) {
			int symbol = 0;
			if (huffman_decode_next(strm, litlen_codes, &symbol)
				!= Z_OK) {
				hm_tree_destroy(litlen_codes);
				hm_tree_destroy(dist_codes);
				return STREAM_TOO_SHORT;
			}
			if (symbol <= 255) {
				push_lit_to_output(strm, (unsigned char)symbol);
			} else if (symbol == 256) {
				break;
			} else {
				if ((read_result = parse_match(strm, symbol, dist_codes)) !=
					Z_OK) {
					
					hm_tree_destroy(litlen_codes);
					hm_tree_destroy(dist_codes);
					return read_result;
				}
			}
		}

		hm_tree_destroy(litlen_codes);
		hm_tree_destroy(dist_codes);
	}

	if (is_last)
		return ZLIB_LAST_BLOCK_PROCESSED;

	return Z_OK;
}

/* Reads next block of imput from strm->src */
static int read_input(z_stream_t *strm)
{
	/* Check for EOF */
	if (Z_EOF(strm->src))
		return STREAM_TOO_SHORT;

	/* Get number of read bytes */

	size_t count = fread(strm->in, 1, CHUNK_SIZE, strm->src);
	if (ferror(strm->src))
		return FILE_ERROR;

	/* Set EOF indicator */

	strm->eof = Z_EOF(strm->src);

	strm->avail_in = (int)count;
	(void)bws_assign_stream(strm->bws, strm->in, count);
	return Z_OK;
}

/* Writes buffered output to strm->dest */
void dump_output(z_stream_t *strm)
{
    size_t written = fwrite(strm->out, 1u, (size_t)strm->avail_out, strm->dest);
	assert(written == (size_t)(strm->avail_out));
	update_adler(&strm->adler, strm->out, strm->avail_out);
    strm->avail_out = 0;
}

static int safe_read_lsbf(z_stream_t *strm, int *data, int nbits)
{
	int remaining = bws_read_lsbf(strm->bws, data, nbits);
	if (remaining) {
		/* Assign next block to BWS */

		int assign_res = read_input(strm);
		if (assign_res != Z_OK)
			return assign_res;

		/* Read remaining bits to the auxiliary buffer and append to data */

		int failsafe = 0;
		(void)bws_read_lsbf(strm->bws, &failsafe, remaining);
		(*data) |= failsafe << (nbits - remaining);
	}
	return Z_OK;
}

static int safe_read_byte(z_stream_t *strm, unsigned char *byte)
{
	/* Check for end of stream */

	if (bwsEOS(strm->bws)) {
		int assign_res = read_input(strm);
		if (assign_res != Z_OK)
			return assign_res;
	}

	/* Read currently processed byte and advance to the beginning of next
	byte */

	*byte = strm->bws->stream[BW_BYTENO(strm->bws)];
	strm->bws->idx = (strm->bws->idx + 8) & ~0x7u;

	return Z_OK;
}

static int read_short_le(z_stream_t *strm, unsigned short *val)
{
	unsigned char b1 = 0, b2 = 0;
	int res1 = safe_read_byte(strm, &b1);
	int res2 = safe_read_byte(strm, &b2);
	if (res1 != Z_OK || res2 != Z_OK)
		return STREAM_TOO_SHORT;
	*val = (unsigned short)(b1 | (b2 << 8));
	return Z_OK;
}

static int read_short_be(z_stream_t *strm, unsigned short *val)
{
	unsigned char b1 = 0, b2 = 0;
	int res1 = safe_read_byte(strm, &b1);
	int res2 = safe_read_byte(strm, &b2);
	if (res1 != Z_OK || res2 != Z_OK)
		return STREAM_TOO_SHORT;
	*val = (unsigned short)(b2 | (b1 << 8));
	return Z_OK;
}

static int read_int_be(z_stream_t *strm, int *val)
{
	unsigned short s1 = 0, s2 = 0;
	int res1 = read_short_be(strm, &s1);
	int res2 = read_short_be(strm, &s2);
	if (res1 != Z_OK || res2 != Z_OK)
		return STREAM_TOO_SHORT;
	*val = s2 | (s1 << 16);
	return Z_OK;
}
