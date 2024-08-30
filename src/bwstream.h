#ifndef _BWSTREAM_H
#define _BWSTREAM_H

#include <stddef.h>

#define BW_M_READ 0x1
#define BW_M_WRITE 0x2

#define BW_BITNO(bws) (bws->idx & 0x7u)
#define BW_BYTENO(bws) (bws->idx >> 3)
#define BW_USED_BYTES(bws) (size_t)((bws->idx + 7u) >> 3u)

typedef struct bw_stream_t {
	int mode;
	size_t idx;
	unsigned char *stream;
	size_t size;
} bw_stream_t;

#define bwsEOS(bws) (bws->idx >= bws->size * 8u)

bw_stream_t *bws_create(int mode);

void bws_destroy(bw_stream_t *bws);

/*	Assign a new stream to the bitwise processor. Returns the old stream.  */
unsigned char *bws_assign_stream(bw_stream_t *bws, unsigned char *stream,
	size_t size);

/*	Skip the current partially processed byte in the stream, saving any
	bits written in write mode.
	Returns 0 on success, or 1 if at end of stream.  */
int bws_flush(bw_stream_t *bws);

/*	Read NBITS to DATA, first bit being stored in the least significant
	(first) bit of the buffer. Returns 0 on success, or the number of bits
	failed to read if end of stream has been reached.
	On successive calls upon failure, read data in a separate buffer and
	concatenate the two (data = data | (data2) << (nbits - remaining)).  */
int bws_read_lsbf(bw_stream_t *bws, int *data, int nbits);

/*	Read NBITS to DATA, first bit being stored in the most significant
	(last) bit of the buffer. Returns 0 on success, or the number of bits
	failed to read if end of stream has been reached.
	Upon failure, successive calls are allowed on the same buffer.  */
int bws_read_msbf(bw_stream_t *bws, int *data, int nbits);

/*	Write NBITS from DATA to the stream, least significant (first) bit being
	written first to the stream.
	Returns 0 on success, or the number of bits failed to write if the end
	of the stream has been reached.
	On successive calls upon failure, the buffer must be left-shifted before
	the call, as to write data in the intended order.
	(data = data >> (nbits - remaining))  */
int bws_write_lsbf(bw_stream_t *bws, int data, int nbits);

/*	Write NBITS from DATA to the stream, most significant (last) bit being
	written first to the stream.
	Returns 0 on success, or the number of bits failed to write if the end
	of the stream has been reached.
	Upon failure, successive calls are allowed on the same buffer.  */
int bws_write_msbf(bw_stream_t *bws, int data, int nbits);

#endif  // _BWSTREAM_H
