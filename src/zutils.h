#ifndef _ZUTILS_H
#define _ZUTILS_H

#include <stdio.h>
#include "bwstream.h"
#include "lzssutils.h"
#include "huffman.h"
#include "zerrcodes.h"

#define ADLER_CONST 65521
#define DEFLATE_BTYPE_LIT 0
#define DEFLATE_BTYPE_FIX 1
#define DEFLATE_BTYPE_DYN 2
#define DEFLATE_BTYPE_ERR 3
#define DEFLATE_HEADER_SIZE 3
#define Z_DYN_TRESHOLD 1024

#define ZLIB_HEADER_LEN 16
#define ZLIB_LAST_BLOCK_PROCESSED 1

#define CINFO_MASK 0xf
#define CINFO_OFFSET 12
#define CM_MASK 0xf
#define CM_OFFSET 8
#define DICT_MASK 0x1
#define DICT_OFFSET 5
#define BTYPE_MASK 0x3
#define BTYPE_OFFSET 0x1

#define MAX_LITLEN_CODES 288
#define MAX_DIST_CODES 32
#define MAX_TOTAL_CODES (MAX_LITLEN_CODES + MAX_DIST_CODES)
#define MAX_ALPHABET_CODES 19

#define HLIT_BITS 5
#define HDIST_BITS 5
#define HCLEN_BITS 4

#define LIT_EXTRA_BITS(code) (((code) < 261 || (code) == 285)\
	? 0 : (((code) - 257) >> 2) - 1)
#define DIST_EXTRA_BITS(code) ((code) < 2 ? 0 : ((code) >> 1) - 1)
#define CLEN_EXTRA_BITS(code) ((code) == 16 ? 2 : ((code) == 17 ? 3 : 7))

extern int len_lookup_table[29];
extern int dist_lookup_table[30];

#define LEN_BASE_VAL(code) (len_lookup_table[(code) - 257])
#define DIST_BASE_VAL(code) (dist_lookup_table[(code)])


#define Z_MODE_INFLATE 0
#define Z_MODE_DEFLATE 1
#define CHUNK_SIZE (1 << 17)	// 131072 , or 128KB

#define Z_EOF(file) (fgetc(file) != EOF ? fseek(file, -1, SEEK_CUR) : 1)

#define UNDEFINED_ERROR (-99)

typedef unsigned char z_byte;

typedef struct z_stream_t {
	FILE *src;
	FILE *dest;
	bw_stream_t *bws;

	circ_buff_t *sliding_window;

	z_byte in[CHUNK_SIZE];
	z_byte out[CHUNK_SIZE];
	int avail_in;					// total bytes in current input block
	int avail_out;					// bytes written in output block buffer

	backlink_array_t *bl_arr;		// for storing len-dist pairs at deflation
	lz_list_t *hashtable;			// easy lookup for potential string matches

	unsigned int adler;
	int mode;						// inflate/read or deflate/write

	int eof;
	int total_out;					/* Only use when deflating, useful for
									accessing back-links */
} z_stream_t;

void luts_init();

void zlib_init(z_stream_t *strm, FILE *src, FILE *dest, int mode);

void zlib_destroy(z_stream_t *strm);

void update_adler(unsigned int *adler, z_byte *vals, int count);

#endif  // _ZUTILS_H
