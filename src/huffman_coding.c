#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include "huffman.h"
#include "huffman_coding.h"
#include "bwstream.h"

#define HMC_MAX_CLEN 32
#define HMC_BUFLEN 1016
#define HMC_OVERFLOW_PROT 8
#define ENDOFSTREAM_SYMBOL 256
#define MAX_REPEAT_LEN 223

void __write_codelengths(int *codelengths, FILE *out)
{
	// Run-length encode canonical tree codelengths and write to file

	uint8_t run_length_enc_clens[257] = {0};
	int n = 1;

	run_length_enc_clens[0] = (uint8_t)codelengths[0];
	int repeat_count = 0;

	for (int i = 1; i < 257; i++) {
		if (repeat_count == MAX_REPEAT_LEN) {
			run_length_enc_clens[n++] = MAX_REPEAT_LEN + HMC_MAX_CLEN;
			repeat_count = 0;
			run_length_enc_clens[n++] = (uint8_t)codelengths[i];
		} else if (codelengths[i] == run_length_enc_clens[n - 1]) {
			repeat_count++;
		} else if (repeat_count) {
			run_length_enc_clens[n++] = (uint8_t)(repeat_count + HMC_MAX_CLEN);
			repeat_count = 0;
			run_length_enc_clens[n++] = (uint8_t)codelengths[i];
		} else {
			run_length_enc_clens[n++] = (uint8_t)codelengths[i];
		}
	}

	// check if last codelength repeats several times

	if (repeat_count) {
		run_length_enc_clens[n++] = (uint8_t)(repeat_count + HMC_MAX_CLEN);
	}

	(void)fwrite(run_length_enc_clens, 1, (size_t)n, out);
}

void huffman_encode(FILE *in, FILE *out)
{
	int weigths[257] = {0};
	weigths[ENDOFSTREAM_SYMBOL] = 1;		// For stream terminator

	// Get symbol frequencies
	int next_byte;
	while ((next_byte = fgetc(in)) != EOF) {
		weigths[next_byte]++;
	}
	rewind(in);

	// Calculate codelengths for huffman tree
	int codelengths[257] = {0};
	hm_get_codelengths(weigths, codelengths, 257, HMC_MAX_CLEN);
	__write_codelengths(codelengths, out);

	// create huffman table
	huffman_tuple *table = hm_create_table(codelengths, 257);

	// set up bitwise buffer
	bw_stream_t *bws = bws_create(BW_M_WRITE);
	uint8_t buffer[HMC_BUFLEN + HMC_OVERFLOW_PROT] = {0};
	(void)bws_assign_stream(bws, buffer, HMC_BUFLEN + HMC_OVERFLOW_PROT);

	// encode data
	while ((next_byte = fgetc(in)) != EOF) {
		(void)bws_write_msbf(bws, table[next_byte].code, table[next_byte].len);

		// avoid overflowing the bitwise processor buffer
		if (BW_USED_BYTES(bws) > HMC_BUFLEN) {
			// flush encoded data to output and move bytes from safety buffer
			// region to the beginning of the buffer
			(void)fwrite(buffer, 1, HMC_BUFLEN, out);
			(void)memmove(buffer, buffer + HMC_BUFLEN, HMC_OVERFLOW_PROT);
			bws->idx -= HMC_BUFLEN << 3;
			(void)memset(buffer + BW_USED_BYTES(bws), 0, HMC_BUFLEN);
		}
	}

	// write terminator and flush all buffered data to output file
	(void)bws_write_msbf(bws, table[ENDOFSTREAM_SYMBOL].code,
		table[ENDOFSTREAM_SYMBOL].len);
	(void)bws_flush(bws);
	(void)fwrite(buffer, 1, BW_USED_BYTES(bws), out);

	// perform cleanup and return
	bws_destroy(bws);
	free(table);
}

void read_codelengths(int *codelengths, FILE *in)
{
	int next_byte = 0;
	int n = 0;

	while (n < 257) {
		next_byte = fgetc(in);

		if (next_byte <= HMC_MAX_CLEN) {
			codelengths[n++] = next_byte;
		} else {
			int repeat_count = next_byte - HMC_MAX_CLEN;
			for (int i = 0; i < repeat_count; i++) {
				codelengths[n] = codelengths[n - 1];
				++n;
			}
		}
	}
}

void huffman_decode(FILE *in, FILE *out)
{
	int codelengths[257] = {0};

	// fetch codelengths from compressed file
	read_codelengths(codelengths, in);

	// create canonical huffman tree
	huffman_tuple *table = hm_create_table(codelengths, 257);
	huffman_tree *tree = hm_create_canonical(table, 257);
	free(table);

	// set up bitwise processor
	uint8_t buffer[HMC_BUFLEN + HMC_OVERFLOW_PROT] = {0};
	bw_stream_t *bws = bws_create(BW_M_READ);
	(void)bws_assign_stream(bws, buffer, HMC_BUFLEN + HMC_OVERFLOW_PROT);
	assert(fread(buffer, 1, HMC_BUFLEN + HMC_OVERFLOW_PROT, in) ==
		   HMC_BUFLEN + HMC_OVERFLOW_PROT);

	// decode data
	huffman_tree *temp;
	int next_byte;
	int input_buffer = 0;
	while (1) {
		// decode next byte
		temp = tree;
		while (1) {
			(void)bws_read_lsbf(bws, &input_buffer, 1);
			temp = input_buffer ? temp->right : temp->left;

			if (HUFFMAN_IS_LEAF(temp)) {
				next_byte = temp->value;
				break;
			}
		}

		// avoid overflowing the bitwise processor buffer
		if (BW_USED_BYTES(bws) > HMC_BUFLEN) {
			(void)memmove(buffer, buffer + HMC_BUFLEN, HMC_OVERFLOW_PROT);
			assert(fread(buffer + HMC_OVERFLOW_PROT, 1, HMC_BUFLEN, in) ==
				   HMC_BUFLEN);
			bws->idx -= HMC_BUFLEN << 3;
		}

		if (next_byte == ENDOFSTREAM_SYMBOL) break;

		// write byte to output
		fputc(next_byte, out);
	}

	// perform cleanup
	hm_tree_destroy(tree);
}
