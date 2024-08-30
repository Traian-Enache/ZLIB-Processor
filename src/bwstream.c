#include "bwstream.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

bw_stream_t *bws_create(int mode)
{
	bw_stream_t *bws = (bw_stream_t *)calloc(1, sizeof(bw_stream_t));
	assert(bws);
	bws->mode = mode;
	return bws;
}

void bws_destroy(bw_stream_t *bws)
{
	free(bws);
}

unsigned char *bws_assign_stream(bw_stream_t *bws, unsigned char *stream,
	size_t size)
{
	assert(stream);
	unsigned char *old_stream = bws->stream;
	bws->stream = stream;
	bws->size = size;
	bws->idx = 0;

	return old_stream;
}

int bws_flush(bw_stream_t *bws)
{
	if (bwsEOS(bws))
		return 1;

	bws->idx = BW_USED_BYTES(bws) << 3;
	return 0;
}

int bws_read_lsbf(bw_stream_t *bws, int *data, int nbits)
{
	if (bws->mode == BW_M_WRITE)
		return -1;
	*data = 0;

	for (int i = 0; i < nbits; i++) {
		if (bwsEOS(bws)) {
			return nbits - i;
		}
		int bit = (bws->stream[BW_BYTENO(bws)] >> BW_BITNO(bws)) & 1;
		(*data) |= bit << i;
		bws->idx++;
	}
	return 0;
}

int bws_read_msbf(bw_stream_t *bws, int *data, int nbits)
{
	if (bws->mode == BW_M_WRITE)
		return -1;

	for (int i = 0; i < nbits; i++) {
		if (bwsEOS(bws)) {
			return nbits - i;
		}

		(*data) <<= 1;
		int bit = (bws->stream[BW_BYTENO(bws)] >> BW_BITNO(bws)) & 1;
		(*data) |= bit;
		bws->idx++;
	}
	return 0;
}

int bws_write_lsbf(bw_stream_t *bws, int data, int nbits)
{
	if (bws->mode == BW_M_READ)
		return -1;

	for (int i = 0; i < nbits; i++) {
		if (bwsEOS(bws)) {
			return nbits - i;
		}

		unsigned char bit = data & 1;

		bws->stream[BW_BYTENO(bws)] |= (unsigned char)(bit << BW_BITNO(bws));
		data >>= 1;
		bws->idx++;
	}
	return 0;
}

int bws_write_msbf(bw_stream_t *bws, int data, int nbits)
{
	if (bws->mode == BW_M_READ)
		return -1;

	for (int i = 0; i < nbits; i++) {
		if (bwsEOS(bws)) {
			return nbits - i;
		}

		unsigned char bit = (unsigned char)(data >> (nbits - i - 1)) & 1u;
		bws->stream[BW_BYTENO(bws)] |= (unsigned char)(bit << BW_BITNO(bws));
		bws->idx++;
	}
	return 0;
}
