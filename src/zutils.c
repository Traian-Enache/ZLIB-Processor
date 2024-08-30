#include "zutils.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>

int len_lookup_table[29] = {0};
int dist_lookup_table[30] = {0};

static void dist_lut_init()
{
	if (dist_lookup_table[0])
		return;

	for (int i = 0; i < 30; i++) {
		if (i < 4) {
			dist_lookup_table[i] = i + 1;
		} else {
			dist_lookup_table[i] =
				dist_lookup_table[i - 1] + (1 << DIST_EXTRA_BITS(i - 1));
		}
	}
}

static void len_lut_init()
{
	if (len_lookup_table[0]) {
		return;
	}

	for (int i = 0; i < 28; i++) {
		if (i < 8) {
			len_lookup_table[i] = i + 3;
		} else {
			len_lookup_table[i] =
				len_lookup_table[i - 1] + (1 << LIT_EXTRA_BITS(i + 256));
		}
	}
	len_lookup_table[28] = 258;
}

void luts_init()
{
	dist_lut_init();
	len_lut_init();
}

void zlib_init(z_stream_t *strm, FILE *src, FILE *dest, int mode)
{
    if (mode == Z_MODE_INFLATE) {
        strm->bws = bws_create(BW_M_READ);
        strm->bl_arr = NULL;
        strm->hashtable = NULL;
    } else if (mode == Z_MODE_DEFLATE) {
        strm->bws = bws_create(BW_M_WRITE);
        strm->bl_arr = bl_arr_create();
        strm->hashtable = (lz_list_t *)calloc(65536, sizeof(lz_list_t));
        assert(strm->hashtable);
    } else {
        fputs("Invalid zlib mode\n", stderr);
        return;
    }

    memset(strm->in, 0, CHUNK_SIZE);
    memset(strm->out, 0, CHUNK_SIZE);
    strm->adler = 1;
    strm->sliding_window = NULL;
    strm->src = src;
    strm->dest = dest;
    strm->mode = mode;
    strm->avail_in = 0;
    strm->avail_out = 0;
	strm->eof = 0;
	strm->total_out = 0;
}

void zlib_destroy(z_stream_t *strm)
{
    if (strm->sliding_window)
        cb_destroy(strm->sliding_window);

    bws_destroy(strm->bws);

    if (strm->mode == Z_MODE_DEFLATE) {
        bl_arr_destroy(strm->bl_arr);
        for (int i = 0; i < 65536; i++) {
            lzl_clear(&strm->hashtable[i]);
        }
        free(strm->hashtable);
    }
}

void update_adler(unsigned int *adler, z_byte *vals, int count)
{
    unsigned s1 = *adler & 0xffff;
    unsigned s2 = (*adler >> 16) & 0xffff;

	for (int i = 0; i < count; i++) {
		s1 = (s1 + vals[i]) % ADLER_CONST;
		s2 = (s2 + s1) % ADLER_CONST;
	}
	*adler = s2 << 16 | s1;
}
